#include "network/async_sender.h"
#include "utils/performance_timer.h"

#include <iostream>
#include <chrono>
#include <functional>

namespace fix_gateway::network
{
    using MessagePtr = fix_gateway::common::MessagePtr;
    using PriorityQueue = fix_gateway::utils::PriorityQueue;
    using LockFreePriorityQueue = fix_gateway::utils::LockFreePriorityQueue;
    using TcpConnection = fix_gateway::network::TcpConnection;

    // Constructor for mutex-based queue (Phase 2)
    AsyncSender::AsyncSender(std::shared_ptr<PriorityQueue> priority_queue,
                             std::shared_ptr<TcpConnection> tcp_connection)
        : priority_queue_(priority_queue),
          lockfree_queue_(nullptr),
          tcp_connection_(tcp_connection),
          use_lockfree_queue_(false),
          running_(false),
          shutdown_requested_(false),
          max_retries_(3),
          base_timeout_(std::chrono::milliseconds(100))
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

    // Constructor for lock-free queue (Phase 3)
    AsyncSender::AsyncSender(std::shared_ptr<LockFreePriorityQueue> lockfree_queue,
                             std::shared_ptr<TcpConnection> tcp_connection)
        : priority_queue_(nullptr),
          lockfree_queue_(lockfree_queue),
          tcp_connection_(tcp_connection),
          use_lockfree_queue_(true),
          running_(false),
          shutdown_requested_(false),
          max_retries_(3),
          base_timeout_(std::chrono::milliseconds(100))
    {
        if (!lockfree_queue_)
        {
            throw std::invalid_argument("LockFreePriorityQueue cannot be null");
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
        stats.current_queue_depth = getQueueSize();
        stats.peak_queue_depth = use_lockfree_queue_ ? 0 : priority_queue_->getPeakSize(); // Lock-free doesn't track peak
        stats.bytes_sent = 0;                                                              // TODO: Track bytes sent
        stats.last_send_time = std::chrono::steady_clock::now();

        return stats;
    }

    size_t AsyncSender::getQueueDepth() const
    {
        return getQueueSize();
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
        if (use_lockfree_queue_)
        {
            senderLoopLockFree();
        }
        else
        {
            senderLoopMutex();
        }
    }

    void AsyncSender::senderLoopMutex()
    {
        fix_gateway::common::MessagePtr message;

        while (running_.load())
        {
            try
            {
                // Try to get a message from the queue with timeout
                if (popMessage(message, std::chrono::milliseconds(10)))
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
                std::cerr << "AsyncSender error in mutex loop: " << e.what() << std::endl;
                // Continue running despite errors
            }
        }

        // Drain remaining messages on shutdown
        while (tryPopMessage(message))
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

    void AsyncSender::senderLoopLockFree()
    {
        fix_gateway::common::MessagePtr message;

        while (running_.load())
        {
            try
            {
                // Lock-free queue only supports tryPop - use busy wait with sleep
                if (tryPopMessage(message))
                {
                    sendMessage(message);
                }
                else
                {
                    // No message available - sleep briefly to avoid busy waiting
                    // This is much shorter than mutex timeout but still CPU-friendly
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }

                // Check for shutdown request
                if (shutdown_requested_.load())
                {
                    break;
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "AsyncSender error in lock-free loop: " << e.what() << std::endl;
                // Continue running despite errors
            }
        }

        // Drain remaining messages on shutdown
        while (tryPopMessage(message))
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

    void AsyncSender::sendMessage(MessagePtr message)
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

    void AsyncSender::handleSendFailure(MessagePtr message)
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

    void AsyncSender::updateStats(MessagePtr message, bool success)
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

    // Queue interface abstraction
    bool AsyncSender::popMessage(MessagePtr &message, std::chrono::milliseconds timeout)
    {
        if (use_lockfree_queue_)
        {
            return lockfree_queue_->tryPop(message);
        }
        else
        {
            return priority_queue_->pop(message, timeout);
        }
    }

    bool AsyncSender::tryPopMessage(MessagePtr &message)
    {
        if (use_lockfree_queue_)
        {
            return lockfree_queue_->tryPop(message);
        }
        else
        {
            return priority_queue_->tryPop(message);
        }
    }

    size_t AsyncSender::getQueueSize() const
    {
        if (use_lockfree_queue_)
        {
            return lockfree_queue_->size();
        }
        else
        {
            return priority_queue_->size();
        }
    }

} // namespace fix_gateway::network
