#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common/constants.h"

namespace fix_gateway::network
{
    class TcpConnection
    {
    public:
        // Callback types for async communication - Zero-copy interface
        using DataCallback = std::function<void(const char *, size_t)>;
        using ErrorCallback = std::function<void(const std::string &)>;
        using DisconnectCallback = std::function<void()>;

        // Constructor/Destructor
        TcpConnection();
        ~TcpConnection();

        // Non-copyable, non-movable
        TcpConnection(const TcpConnection &) = delete;
        TcpConnection &operator=(const TcpConnection &) = delete;
        TcpConnection(TcpConnection &&) = delete;
        TcpConnection &operator=(TcpConnection &&) = delete;

        // Step 1: Socket Creation & Initialization
        void createSocket();
        void configureSocket();

        // Step 2: Connection Establishment
        bool connect(const std::string &host, int port);
        void setupSocketAddress(const std::string &host, int port);
        bool handleConnectionResult(int result);

        // Step 3: Data Sending
        bool send(const std::string &message);
        bool send(const std::vector<char> &data);
        ssize_t sendRaw(const void *data, size_t length);
        bool handlePartialSend(const void *data, size_t length, ssize_t bytesSent);

        // Step 4: Async Data Receiving
        void startReceiveLoop();
        void receiveLoop();
        void onDataReceived(const char *data, size_t length);

        // Step 5: Connection Management
        bool isConnected() const;
        void disconnect();
        void handleConnectionLost();
        bool reconnect();

        // Step 6: Error Handling
        void handleSocketError(int error);
        void onError(const std::string &errorMessage);
        std::string getLastError() const;

        // Step 7: Resource Cleanup
        void cleanup();
        void stopReceiveLoop();

        // Callback setters
        void setDataCallback(DataCallback callback);
        void setErrorCallback(ErrorCallback callback);
        void setDisconnectCallback(DisconnectCallback callback);

        // Connection info
        std::string getRemoteHost() const;
        int getRemotePort() const;

    private:
        // Socket members
        int socket_fd_;
        struct sockaddr_in server_addr_;

        // Connection state (thread-safe)
        std::atomic<bool> connected_;
        std::atomic<bool> receiving_;

        // Connection info
        std::string host_;
        int port_;

        // Threading
        std::thread receive_thread_;

        // Buffers
        std::vector<char> receive_buffer_;
        mutable std::mutex buffer_mutex_;

        // Error handling
        std::string last_error_;
        mutable std::mutex error_mutex_;

        // Callbacks
        DataCallback data_callback_;
        ErrorCallback error_callback_;
        DisconnectCallback disconnect_callback_;
        mutable std::mutex callback_mutex_;

        // Note: Constants moved to common/constants.h
    };
} // namespace fix_gateway::network