#pragma once

#include "network/tcp_connection.h"
#include "protocol/stream_fix_parser.h"
#include "protocol/fix_message.h"
#include "common/message_pool.h"
#include <functional>
#include <memory>

namespace fix_gateway::application
{
    // Integration layer that connects TCP connection with FIX parser
    // Demonstrates complete zero-copy data flow: TCP → Parser → Trading Logic
    class FixGateway
    {
    public:
        // Callback for parsed FIX messages
        using MessageCallback = std::function<void(protocol::FixMessage *)>;
        using ErrorCallback = std::function<void(const std::string &)>;

        // Constructor
        explicit FixGateway(size_t message_pool_size = 8192);

        // Destructor
        ~FixGateway();

        // Non-copyable
        FixGateway(const FixGateway &) = delete;
        FixGateway &operator=(const FixGateway &) = delete;

        // =================================================================
        // CONNECTION MANAGEMENT
        // =================================================================

        // Connect to FIX server
        bool connect(const std::string &host, int port);

        // Disconnect from server
        void disconnect();

        // Check connection status
        bool isConnected() const;

        // =================================================================
        // MESSAGE HANDLING SETUP
        // =================================================================

        // Set callback for parsed FIX messages
        void setMessageCallback(MessageCallback callback);

        // Set error callback
        void setErrorCallback(ErrorCallback callback);

        // =================================================================
        // SENDING MESSAGES
        // =================================================================

        // Send FIX message (will be serialized)
        bool sendMessage(protocol::FixMessage *message);

        // Send raw FIX string
        bool sendRawMessage(const std::string &fix_message);

        // =================================================================
        // PERFORMANCE MONITORING
        // =================================================================

        // Get parser statistics
        const protocol::StreamFixParser::ParserStats &getParserStats() const;

        // Reset parser statistics
        void resetParserStats();

        // Get message pool statistics
        common::MessagePool<protocol::FixMessage>::PoolStats getPoolStats() const;

        // =================================================================
        // CONFIGURATION
        // =================================================================

        // Configure parser settings
        void setMaxMessageSize(size_t max_size);
        void setValidateChecksum(bool validate);
        void setStrictValidation(bool strict);

    private:
        // =================================================================
        // CORE DATA FLOW - This is where the magic happens!
        // =================================================================

        // TCP data callback - receives raw network buffer
        void onTcpDataReceived(const char *buffer, size_t length);

        // TCP error callback
        void onTcpError(const std::string &error);

        // TCP disconnect callback
        void onTcpDisconnect();

        // Process parsed FIX message
        void processParsedMessage(protocol::FixMessage *message);

        // =================================================================
        // MEMBER VARIABLES
        // =================================================================

        // Core components
        std::unique_ptr<network::TcpConnection> tcp_connection_;
        std::unique_ptr<protocol::StreamFixParser> fix_parser_;
        std::unique_ptr<common::MessagePool<protocol::FixMessage>> message_pool_;

        // Callbacks
        MessageCallback message_callback_;
        ErrorCallback error_callback_;

        // Connection state
        bool connected_;
    };

} // namespace fix_gateway::application