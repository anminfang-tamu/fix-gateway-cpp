#include "application/fix_gateway.h"
#include "utils/logger.h"
#include <iostream>

namespace fix_gateway::application
{
    using namespace fix_gateway::network;
    using namespace fix_gateway::protocol;
    using namespace fix_gateway::common;

    FixGateway::FixGateway(size_t message_pool_size)
        : connected_(false)
    {
        // Create message pool first
        message_pool_ = std::make_unique<MessagePool<FixMessage>>(message_pool_size);

        // Create FIX parser with message pool
        fix_parser_ = std::make_unique<StreamFixParser>(message_pool_.get());

        // Create TCP connection
        tcp_connection_ = std::make_unique<TcpConnection>();

        // =================================================================
        // CRITICAL INTEGRATION: Setup TCP callbacks to flow to FIX parser
        // =================================================================

        // Set TCP data callback - THIS IS THE MAIN DATA FLOW!
        tcp_connection_->setDataCallback(
            [this](const char *buffer, size_t length)
            {
                onTcpDataReceived(buffer, length); // Raw buffer → FIX parser
            });

        // Set TCP error callback
        tcp_connection_->setErrorCallback(
            [this](const std::string &error)
            {
                onTcpError(error);
            });

        // Set TCP disconnect callback
        tcp_connection_->setDisconnectCallback(
            [this]()
            {
                onTcpDisconnect();
            });
    }

    FixGateway::~FixGateway()
    {
        disconnect();
    }

    // =================================================================
    // CONNECTION MANAGEMENT
    // =================================================================

    bool FixGateway::connect(const std::string &host, int port)
    {
        if (connected_)
        {
            LOG_WARN("Already connected");
            return true;
        }

        LOG_INFO("Connecting to FIX server: " + host + ":" + std::to_string(port));

        if (tcp_connection_->connect(host, port))
        {
            connected_ = true;
            tcp_connection_->startReceiveLoop(); // Start receiving data
            LOG_INFO("Connected to FIX server successfully");
            return true;
        }

        LOG_ERROR("Failed to connect to FIX server");
        return false;
    }

    void FixGateway::disconnect()
    {
        if (connected_)
        {
            LOG_INFO("Disconnecting from FIX server");
            tcp_connection_->disconnect();
            connected_ = false;
        }
    }

    bool FixGateway::isConnected() const
    {
        return connected_ && tcp_connection_->isConnected();
    }

    // =================================================================
    // CORE DATA FLOW IMPLEMENTATION - THE MAGIC HAPPENS HERE!
    // =================================================================

    void FixGateway::onTcpDataReceived(const char *buffer, size_t length)
    {
        // This is called directly from TCP receive thread with raw network buffer
        // NO MEMORY ALLOCATIONS - pure zero-copy operation!

        LOG_DEBUG("Received " + std::to_string(length) + " bytes from TCP");

        // =================================================================
        // ZERO-COPY PARSING: Raw Buffer → FixMessage* (from pool)
        // =================================================================

        try
        {
            // Parse the buffer - this is where the magic happens!
            auto parse_result = fix_parser_->parse(buffer, length);

            switch (parse_result.status)
            {
            case StreamFixParser::ParseStatus::Success:
            {
                LOG_DEBUG("Successfully parsed FIX message");

                // Process the parsed message
                processParsedMessage(parse_result.parsed_message);

                // Return message to pool when done
                message_pool_->deallocate(parse_result.parsed_message);
                break;
            }

            case StreamFixParser::ParseStatus::NeedMoreData:
            {
                LOG_DEBUG("Partial message received, waiting for more data");
                // Parser automatically handles partial messages internally
                break;
            }

            case StreamFixParser::ParseStatus::InvalidFormat:
            {
                LOG_ERROR("Invalid FIX message format: " + parse_result.error_detail);
                if (error_callback_)
                {
                    error_callback_("Parse error: " + parse_result.error_detail);
                }
                break;
            }

            case StreamFixParser::ParseStatus::ChecksumError:
            {
                LOG_ERROR("FIX message checksum error");
                if (error_callback_)
                {
                    error_callback_("Checksum validation failed");
                }
                break;
            }

            case StreamFixParser::ParseStatus::AllocationFailed:
            {
                LOG_ERROR("MessagePool allocation failed - pool exhausted?");
                if (error_callback_)
                {
                    error_callback_("Message pool allocation failed");
                }
                break;
            }

            default:
                LOG_ERROR("Unknown parse error");
                break;
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception during FIX parsing: " + std::string(e.what()));
            if (error_callback_)
            {
                error_callback_("Parse exception: " + std::string(e.what()));
            }
        }
    }

    void FixGateway::processParsedMessage(FixMessage *message)
    {
        if (!message)
        {
            LOG_ERROR("Null message passed to processParsedMessage");
            return;
        }

        // Log message details
        LOG_INFO("Processed FIX message: " + message->getFieldsSummary());

        // Call user callback if set
        if (message_callback_)
        {
            try
            {
                message_callback_(message); // Pass raw pointer to user code
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Exception in message callback: " + std::string(e.what()));
            }
        }
    }

    void FixGateway::onTcpError(const std::string &error)
    {
        LOG_ERROR("TCP error: " + error);
        if (error_callback_)
        {
            error_callback_("TCP error: " + error);
        }
    }

    void FixGateway::onTcpDisconnect()
    {
        LOG_INFO("TCP connection lost");
        connected_ = false;
        if (error_callback_)
        {
            error_callback_("Connection lost");
        }
    }

    // =================================================================
    // CALLBACK SETUP
    // =================================================================

    void FixGateway::setMessageCallback(MessageCallback callback)
    {
        message_callback_ = callback;
    }

    void FixGateway::setErrorCallback(ErrorCallback callback)
    {
        error_callback_ = callback;
    }

    // =================================================================
    // SENDING MESSAGES
    // =================================================================

    bool FixGateway::sendMessage(FixMessage *message)
    {
        if (!connected_ || !message)
        {
            return false;
        }

        try
        {
            std::string serialized = message->toString();
            return tcp_connection_->send(serialized);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to send message: " + std::string(e.what()));
            return false;
        }
    }

    bool FixGateway::sendRawMessage(const std::string &fix_message)
    {
        if (!connected_)
        {
            return false;
        }

        return tcp_connection_->send(fix_message);
    }

    // =================================================================
    // PERFORMANCE MONITORING
    // =================================================================

    const StreamFixParser::ParserStats &FixGateway::getParserStats() const
    {
        return fix_parser_->getStats();
    }

    void FixGateway::resetParserStats()
    {
        fix_parser_->resetStats();
    }

    MessagePool<FixMessage>::PoolStats FixGateway::getPoolStats() const
    {
        return message_pool_->getStats();
    }

    // =================================================================
    // CONFIGURATION
    // =================================================================

    void FixGateway::setMaxMessageSize(size_t max_size)
    {
        fix_parser_->setMaxMessageSize(max_size);
    }

    void FixGateway::setValidateChecksum(bool validate)
    {
        fix_parser_->setValidateChecksum(validate);
    }

    void FixGateway::setStrictValidation(bool strict)
    {
        fix_parser_->setStrictValidation(strict);
    }

} // namespace fix_gateway::application