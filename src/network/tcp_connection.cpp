#include "network/tcp_connection.h"
#include "utils/logger.h"
#include "common/constants.h"
#include <fcntl.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <cstring>
#include <stdexcept>

namespace fix_gateway::network
{
    using namespace constants; // For cleaner constant usage

    TcpConnection::TcpConnection()
        : socket_fd_(INVALID_SOCKET), connected_(false), receiving_(false), port_(0) {}

    TcpConnection::~TcpConnection()
    {
        cleanup();
    }

    void TcpConnection::createSocket()
    {
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd_ == INVALID_SOCKET)
        {
            throw std::runtime_error("Failed to create socket");
        }
        LOG_INFO("Socket created successfully");
    }

    void TcpConnection::configureSocket()
    {
        // 1. SO_REUSEADDR - Allow socket reuse after close
        int reuse = 1;
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        {
            throw std::runtime_error("Failed to set SO_REUSEADDR");
        }
        LOG_DEBUG("SO_REUSEADDR configured");

        // 2. TCP_NODELAY - CRITICAL for trading systems (disable Nagle's algorithm)
        // no buffer small packets, just send them
        int nodelay = 1;
        if (setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0)
        {
            throw std::runtime_error("Failed to set TCP_NODELAY");
        }
        LOG_DEBUG("TCP_NODELAY configured - low latency mode enabled");

        // 3. SO_KEEPALIVE - Detect dead connections
        int keepalive = 1;
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0)
        {
            throw std::runtime_error("Failed to set SO_KEEPALIVE");
        }
        LOG_DEBUG("SO_KEEPALIVE configured");

        // 4. Set non-blocking mode for async I/O
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        if (flags < 0)
        {
            throw std::runtime_error("Failed to get socket flags");
        }
        if (fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            throw std::runtime_error("Failed to set non-blocking mode");
        }
        LOG_DEBUG("Non-blocking mode configured");

        // 5. SO_LINGER - Clean shutdown (immediate close)
        struct linger ling;
        ling.l_onoff = 1;  // Enable linger
        ling.l_linger = 0; // Immediate close
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling)) < 0)
        {
            throw std::runtime_error("Failed to set SO_LINGER");
        }
        LOG_DEBUG("SO_LINGER configured");

        // 6. Optional: Set buffer sizes for better performance
        int rcvbuf = LARGE_BUFFER_SIZE; // 64KB receive buffer
        int sndbuf = LARGE_BUFFER_SIZE; // 64KB send buffer

        if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0)
        {
            LOG_WARN("Failed to set receive buffer size - continuing anyway");
        }
        else
        {
            LOG_DEBUG("Receive buffer size set to 64KB");
        }

        if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0)
        {
            LOG_WARN("Failed to set send buffer size - continuing anyway");
        }
        else
        {
            LOG_DEBUG("Send buffer size set to 64KB");
        }

        LOG_INFO("Socket configured successfully for trading system");
    }

    void TcpConnection::setupSocketAddress(const std::string &host, int port)
    {
        // Store for future reference
        host_ = host;
        port_ = port;

        // Clear the address structure
        memset(&server_addr_, 0, sizeof(server_addr_));

        // Set address family
        server_addr_.sin_family = AF_INET;

        // Set port (convert to network byte order)
        server_addr_.sin_port = htons(port);

        // Convert hostname/IP to address
        if (inet_addr(host.c_str()) != INADDR_NONE)
        {
            // It's already an IP address
            server_addr_.sin_addr.s_addr = inet_addr(host.c_str());
        }
        else
        {
            // It's a hostname, need to resolve it
            struct hostent *he = gethostbyname(host.c_str());
            if (he == nullptr)
            {
                throw std::runtime_error("Failed to resolve hostname: " + host);
            }
            memcpy(&server_addr_.sin_addr, he->h_addr_list[0], he->h_length);
        }

        LOG_INFO("Socket address configured for " + host + ":" + std::to_string(port));
    }

    bool TcpConnection::handleConnectionResult(int result)
    {
        if (result < 0)
        {
            LOG_ERROR("Failed to connect to server");
            return false;
        }
        LOG_INFO("Connected to server successfully");
        return true;
    }

    bool TcpConnection::connect(const std::string &host, int port)
    {
        setupSocketAddress(host, port);
        createSocket();
        configureSocket();
        int result = ::connect(socket_fd_, (struct sockaddr *)&server_addr_, sizeof(server_addr_));
        return handleConnectionResult(result);
    }
}