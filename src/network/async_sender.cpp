#include "network/async_sender.h"
#include "utils/performance_timer.h"

#include <iostream>
#include <chrono>
#include <functional>

namespace fix_gateway::network
{
    AsyncSender::AsyncSender(std::shared_ptr<fix_gateway::utils::PriorityQueue> priority_queue,
                             std::shared_ptr<fix_gateway::network::TcpConnection> tcp_connection)
        : priority_queue_(priority_queue), tcp_connection_(tcp_connection), running_(false), shutdown_requested_(false), max_retries_(3), base_timeout_(std::chrono::milliseconds(100))
    {
        if (!priority_queue_)
        {
            throw std::invalid_argument("PriorityQueue cannot be null");
        }
        if (!tcp_connection_)
        {
            throw std::invalid_argument("TcpConnection cannot be null");
        }
    }

    AsyncSender::~AsyncSender()
    {
        shutdown();
    }

    void AsyncSender::start()
    {
        if (running_.load())
        {
            return; // Already running
        }

        running_.store(true);
        shutdown_requested_.store(false);

        sender_thread_ = std::thread(&AsyncSender::senderLoop, this);
    }

    void AsyncSender::stop()
    {
        running_.store(false);

        if (sender_thread_.joinable())
        {
            sender_thread_.join();
        }
    }

    void AsyncSender::shutdown(std::chrono::seconds timeout)
    {
        if (!running_.load())
        {
            return; // Already shutdown
        }

        shutdown_requested_.store(true);
        running_.store(false);

        // Wait for sender thread to finish with timeout
        if (sender_thread_.joinable())
        {
            auto start_time = std::chrono::steady_clock::now();

            // Try to join with timeout
            std::thread timeout_thread([this, timeout]()
                                       {
                                           std::this_thread::sleep_for(timeout);
                                           // Force exit if still running after timeout
                                       });

            sender_thread_.join();

            if (timeout_thread.joinable())
            {
                timeout_thread.detach(); // Let it finish naturally
            }
        }
    }

    bool AsyncSender::isRunning() const
    {
        return running_.load();
    }

    SenderStats AsyncSender::getStats() const
    {
        SenderStats stats;
        stats.total_messages_sent = total_sent_.load();
        stats.total_messages_failed = total_failed_.load();
        stats.total_messages_retried = total_retried_.load();
        stats.total_messages_dropped = 0; // Calculated from queue stats
        stats.messages_in_flight = 0;     // TODO: Track in-flight messages
        stats.avg_send_latency_ns = 0.0;  // TODO: Implement latency tracking
        stats.avg_queue_latency_ns = 0.0;
        stats.current_queue_depth = priority_queue_->size();
        stats.peak_queue_depth = priority_queue_->getPeakSize();
        stats.bytes_sent = 0; // TODO: Track bytes sent
        stats.last_send_time = std::chrono::steady_clock::now();

        return stats;
    }

    size_t AsyncSender::getQueueDepth() const
    {
        return priority_queue_->size();
    }

    bool AsyncSender::isConnected() const
    {
        return tcp_connection_ && tcp_connection_->isConnected();
    }

    void AsyncSender::setMaxRetries(size_t max_retries)
    {
        max_retries_ = max_retries;
    }

    void AsyncSender::setBatchSize(size_t size)
    {
        batch_size_ = size;
        enable_batch_processing_ = (size > 1);
    }

    void AsyncSender::senderLoop()
    {
        fix_gateway::common::MessagePtr message;

        while (running_.load())
        {
            try
            {
                // Try to get a message from the queue with timeout
                if (priority_queue_->pop(message, std::chrono::milliseconds(10)))
                {
                    sendMessage(message);
                }

                // Check for shutdown request
                if (shutdown_requested_.load())
                {
                    break;
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "AsyncSender error in main loop: " << e.what() << std::endl;
                // Continue running despite errors
            }
        }

        // Drain remaining messages on shutdown
        while (priority_queue_->tryPop(message))
        {
            try
            {
                sendMessage(message);
            }
            catch (const std::exception &e)
            {
                std::cerr << "AsyncSender error during shutdown drain: " << e.what() << std::endl;
                break; // Stop draining on errors during shutdown
            }
        }
    }

    void AsyncSender::sendMessage(fix_gateway::common::MessagePtr message)
    {
        if (!message)
        {
            return;
        }

        auto start_time = std::chrono::high_resolution_clock::now();
        bool success = false;
        size_t retry_count = 0;

        while (retry_count <= max_retries_ && running_.load())
        {
            try
            {
                // Check connection status
                if (!isConnected())
                {
                    throw std::runtime_error("TCP connection is not available");
                }

                // Attempt to send the message
                std::string message_data = message->getPayload();
                tcp_connection_->send(message_data);

                success = true;
                break;
            }
            catch (const std::exception &e)
            {
                retry_count++;

                if (retry_count <= max_retries_)
                {
                    total_retried_.fetch_add(1);

                    // Exponential backoff for retries
                    auto delay = calculateTimeout() * retry_count;
                    std::this_thread::sleep_for(delay);
                }
                else
                {
                    // Max retries exceeded
                    handleSendFailure(message);
                    if (error_callback_)
                    {
                        error_callback_(message, std::string("Max retries exceeded: ") + e.what());
                    }
                }
            }
        }

        // Update statistics
        updateStats(message, success);

        if (success)
        {
            total_sent_.fetch_add(1);
        }
        else
        {
            total_failed_.fetch_add(1);
        }
    }

    void AsyncSender::handleSendFailure(fix_gateway::common::MessagePtr message)
    {
        // Log the failure
        std::cerr << "Failed to send message after " << max_retries_ << " retries. "
                  << "Message ID: " << message->getMessageId()
                  << ", Priority: " << static_cast<int>(message->getPriority()) << std::endl;

        // TODO: Implement additional failure handling:
        // - Dead letter queue for failed messages
        // - Circuit breaker pattern
        // - Alternative routing
        // - Alerting system integration
    }

    std::chrono::milliseconds AsyncSender::calculateTimeout() const
    {
        // Simple timeout calculation - can be made more sophisticated
        return base_timeout_;
    }

    void AsyncSender::updateStats(fix_gateway::common::MessagePtr message, bool success)
    {
        // TODO: Implement comprehensive statistics tracking:
        // - Message latency (queue time + send time)
        // - Bytes sent tracking
        // - Per-priority statistics
        // - Rolling averages and percentiles
        // - Integration with performance counters

        if (message && success)
        {
            // Calculate and record latency
            auto end_time = std::chrono::high_resolution_clock::now();
            // Note: Would need to store queue entry time in message for full latency calculation
        }
    }

    void AsyncSender::resetStats()
    {
        total_sent_.store(0);
        total_failed_.store(0);
        total_retried_.store(0);
    }

    // Thread management (for core pinning)
    std::thread &AsyncSender::getSenderThread()
    {
        if (!isThreadJoinable())
        {
            throw std::runtime_error("AsyncSender thread is not joinable - call start() first");
        }
        return sender_thread_;
    }

    bool AsyncSender::isThreadJoinable() const
    {
        return sender_thread_.joinable();
    }

} // namespace fix_gateway::network
