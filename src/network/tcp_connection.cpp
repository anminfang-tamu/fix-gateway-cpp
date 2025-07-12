#include "network/tcp_connection.h"
#include "utils/logger.h"
#include "utils/performance_timer.h"
#include "utils/performance_counters.h"
#include "common/constants.h"
#include <fcntl.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <unistd.h>

namespace fix_gateway::network
{
    using namespace constants;      // For cleaner constant usage
    using namespace utils::metrics; // For performance metrics

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

    bool TcpConnection::send(const std::string &message)
    {
        PERF_FUNCTION_TIMER(); // Measure total send latency

        if (!connected_)
        {
            LOG_ERROR("Cannot send: not connected");
            PERF_COUNTER_INC(CONNECTION_ERRORS);
            return false;
        }

        if (message.empty())
        {
            LOG_WARN("Attempting to send empty message");
            return true;
        }

        PERF_TIMER_START(send_operation);

        ssize_t bytes_sent = sendRaw(message.c_str(), message.size());
        if (bytes_sent < 0)
        {
            LOG_ERROR("Failed to send string message");
            PERF_COUNTER_INC(CONNECTION_ERRORS);
            return false;
        }

        // Handle partial send
        if (static_cast<size_t>(bytes_sent) < message.size())
        {
            bool success = handlePartialSend(message.c_str(), message.size(), bytes_sent);
            PERF_TIMER_END(send_operation);

            if (success)
            {
                PERF_COUNTER_ADD(BYTES_SENT, message.size());
                PERF_COUNTER_INC(MESSAGES_SENT);
                PERF_RATE_RECORD(SEND_RATE);
            }
            return success;
        }

        PERF_TIMER_END(send_operation);

        // Record successful send metrics
        PERF_COUNTER_ADD(BYTES_SENT, bytes_sent);
        PERF_COUNTER_INC(MESSAGES_SENT);
        PERF_RATE_RECORD(SEND_RATE);

        LOG_DEBUG("Sent " + std::to_string(bytes_sent) + " bytes");
        return true;
    }

    bool TcpConnection::send(const std::vector<char> &data)
    {
        if (!connected_)
        {
            LOG_ERROR("Cannot send: not connected");
            return false;
        }

        if (data.empty())
        {
            LOG_WARN("Attempting to send empty data");
            return true;
        }

        ssize_t bytes_sent = sendRaw(data.data(), data.size());
        if (bytes_sent < 0)
        {
            LOG_ERROR("Failed to send vector data");
            return false;
        }

        // Handle partial send
        if (static_cast<size_t>(bytes_sent) < data.size())
        {
            return handlePartialSend(data.data(), data.size(), bytes_sent);
        }

        LOG_DEBUG("Sent " + std::to_string(bytes_sent) + " bytes");
        return true;
    }

    ssize_t TcpConnection::sendRaw(const void *data, size_t length)
    {
        if (socket_fd_ == INVALID_SOCKET)
        {
            LOG_ERROR("Invalid socket for sending");
            return -1;
        }

        // Use send() with MSG_NOSIGNAL to avoid SIGPIPE on broken connections
        ssize_t result = ::send(socket_fd_, data, length, MSG_NOSIGNAL);

        if (result < 0)
        {
            // Log the specific error
            handleSocketError(errno);
            return -1;
        }

        return result;
    }

    bool TcpConnection::handlePartialSend(const void *data, size_t length, ssize_t bytesSent)
    {
        LOG_WARN("Partial send detected: " + std::to_string(bytesSent) + "/" + std::to_string(length) + " bytes sent");

        const char *remaining_data = static_cast<const char *>(data) + bytesSent;
        size_t remaining_length = length - bytesSent;

        // Try to send the remaining data
        while (remaining_length > 0)
        {
            ssize_t result = sendRaw(remaining_data, remaining_length);

            if (result < 0)
            {
                LOG_ERROR("Failed to send remaining data");
                return false;
            }

            if (result == 0)
            {
                LOG_ERROR("Connection closed during partial send");
                connected_ = false;
                return false;
            }

            remaining_data += result;
            remaining_length -= result;

            LOG_DEBUG("Sent additional " + std::to_string(result) + " bytes, " +
                      std::to_string(remaining_length) + " remaining");
        }

        LOG_INFO("Successfully completed partial send");
        return true;
    }

    void TcpConnection::handleSocketError(int error)
    {
        std::string error_msg;

        switch (error)
        {
        case ECONNRESET:
            error_msg = "Connection reset by peer";
            connected_ = false;
            break;
        case EPIPE:
            error_msg = "Broken pipe (connection closed)";
            connected_ = false;
            break;
        case EWOULDBLOCK: // non-blocking socket and unix/linux may have different values for EWOULDBLOCK and EAGAIN
#if EAGAIN != EWOULDBLOCK
        case EAGAIN:
#endif
            error_msg = "Send would block (non-blocking socket)";
            break;
        case ENOTCONN:
            error_msg = "Socket not connected";
            connected_ = false;
            break;
        case EINTR:
            error_msg = "Send interrupted by signal";
            break;
        default:
            error_msg = "Socket error: " + std::string(strerror(error));
            break;
        }

        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            last_error_ = error_msg;
        }

        LOG_ERROR("Socket error [" + std::to_string(error) + "]: " + error_msg);

        // Call error callback if set
        if (error_callback_)
        {
            error_callback_(error_msg);
        }

        // If connection is lost, call disconnect callback
        if (!connected_ && disconnect_callback_)
        {
            disconnect_callback_();
        }
    }

    void TcpConnection::startReceiveLoop()
    {
        if (receiving_)
        {
            LOG_WARN("Receive loop already running");
            return;
        }

        if (!connected_)
        {
            LOG_ERROR("Cannot start receive loop: not connected");
            return;
        }

        receiving_ = true;
        receive_thread_ = std::thread(&TcpConnection::receiveLoop, this);
        LOG_INFO("Receive loop started");
    }

    void TcpConnection::receiveLoop()
    {
        std::vector<char> buffer(BUFFER_SIZE);

        LOG_DEBUG("Entering receive loop");

        while (receiving_ && connected_)
        {
            // Receive data from socket
            ssize_t bytes_received = ::recv(socket_fd_, buffer.data(), buffer.size(), MSG_DONTWAIT);

            if (bytes_received > 0)
            {
                // Got data - process it with timing
                PERF_TIMER_START(receive_processing);

                LOG_DEBUG("Received " + std::to_string(bytes_received) + " bytes");
                handleIncomingData(buffer.data(), bytes_received);

                PERF_TIMER_END(receive_processing);

                // Record receive metrics
                PERF_COUNTER_ADD(BYTES_RECEIVED, bytes_received);
                PERF_COUNTER_INC(MESSAGES_RECEIVED);
                PERF_RATE_RECORD(RECEIVE_RATE);
            }
            else if (bytes_received == 0)
            {
                // Connection closed by peer
                LOG_INFO("Connection closed by peer");
                connected_ = false;
                handleConnectionLost();
                break;
            }
            else
            {
                // Error occurred - use our centralized error handler
                int error = errno;
                handleSocketError(error);

                // Check if we should continue based on the error type
                if (error == EWOULDBLOCK || error == EAGAIN)
                {
                    // No data available right now - normal for non-blocking sockets
                    // Sleep briefly to avoid busy waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                else if (error == EINTR)
                {
                    // Interrupted by signal - continue
                    continue;
                }
                else
                {
                    // For all other errors (including connection lost), exit the loop
                    // handleSocketError already handled state changes and callbacks
                    break;
                }
            }
        }

        LOG_DEBUG("Exiting receive loop");
        receiving_ = false;
    }

    void TcpConnection::stopReceiveLoop()
    {
        if (!receiving_)
        {
            LOG_DEBUG("Receive loop not running");
            return;
        }

        LOG_INFO("Stopping receive loop");
        receiving_ = false;

        // Wait for the thread to finish
        if (receive_thread_.joinable())
        {
            receive_thread_.join();
            LOG_DEBUG("Receive thread joined successfully");
        }
    }

    void TcpConnection::handleIncomingData(const char *data, size_t length)
    {
        if (length == 0 || data == nullptr)
        {
            return;
        }

        // Create a vector with the received data
        std::vector<char> received_data(data, data + length);

        // Store in receive buffer (for potential future processing)
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            receive_buffer_.insert(receive_buffer_.end(), received_data.begin(), received_data.end());
        }

        // Call the data callback
        onDataReceived(received_data);
    }

    void TcpConnection::onDataReceived(const std::vector<char> &data)
    {
        // Call the registered callback if set
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (data_callback_)
            {
                try
                {
                    data_callback_(data);
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR("Exception in data callback: " + std::string(e.what()));
                }
                catch (...)
                {
                    LOG_ERROR("Unknown exception in data callback");
                }
            }
            else
            {
                LOG_DEBUG("No data callback registered, " + std::to_string(data.size()) + " bytes discarded");
            }
        }
    }

    void TcpConnection::handleConnectionLost()
    {
        LOG_WARN("Handling connection lost");

        connected_ = false;

        // Call disconnect callback if set
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (disconnect_callback_)
            {
                try
                {
                    disconnect_callback_();
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR("Exception in disconnect callback: " + std::string(e.what()));
                }
                catch (...)
                {
                    LOG_ERROR("Unknown exception in disconnect callback");
                }
            }
        }
    }

    bool TcpConnection::isConnected() const
    {
        return connected_;
    }

    void TcpConnection::disconnect()
    {
        if (!connected_)
        {
            LOG_DEBUG("Already disconnected");
            return;
        }

        LOG_INFO("Disconnecting...");

        // Stop receiving first
        stopReceiveLoop();

        // Mark as disconnected
        connected_ = false;

        // Close socket
        if (socket_fd_ != constants::INVALID_SOCKET)
        {
            if (::close(socket_fd_) < 0)
            {
                LOG_WARN("Error closing socket: " + std::string(strerror(errno)));
            }
            socket_fd_ = constants::INVALID_SOCKET;
            LOG_DEBUG("Socket closed");
        }

        LOG_INFO("Disconnected successfully");
    }

    void TcpConnection::cleanup()
    {
        LOG_DEBUG("Cleaning up TCP connection");

        // Stop receive loop and disconnect
        stopReceiveLoop();

        if (connected_)
        {
            disconnect();
        }

        // Clear buffers
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            receive_buffer_.clear();
        }

        // Clear callbacks
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            data_callback_ = nullptr;
            error_callback_ = nullptr;
            disconnect_callback_ = nullptr;
        }

        LOG_DEBUG("TCP connection cleanup completed");
    }

    // Callback setters
    void TcpConnection::setDataCallback(DataCallback callback)
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        data_callback_ = callback;
        LOG_DEBUG("Data callback set");
    }

    void TcpConnection::setErrorCallback(ErrorCallback callback)
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        error_callback_ = callback;
        LOG_DEBUG("Error callback set");
    }

    void TcpConnection::setDisconnectCallback(DisconnectCallback callback)
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        disconnect_callback_ = callback;
        LOG_DEBUG("Disconnect callback set");
    }

    // Connection info getters
    std::string TcpConnection::getRemoteHost() const
    {
        return host_;
    }

    int TcpConnection::getRemotePort() const
    {
        return port_;
    }

    std::string TcpConnection::getLastError() const
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return last_error_;
    }

}