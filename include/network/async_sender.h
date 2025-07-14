#pragma once

#include "utils/priority_queue.h"
#include "network/tcp_connection.h"
#include "common/message.h"

#include <atomic>
#include <memory>
#include <chrono>
#include <thread>

namespace fix_gateway::network
{
    using MessagePtr = fix_gateway::common::MessagePtr;
    using PriorityQueue = fix_gateway::utils::PriorityQueue;
    using TcpConnection = fix_gateway::network::TcpConnection;

    struct SenderStats
    {
        size_t total_messages_sent;
        size_t total_messages_failed;
        size_t total_messages_retried;
        size_t total_messages_dropped;
        size_t messages_in_flight;
        double avg_send_latency_ns;
        double avg_queue_latency_ns;
        size_t current_queue_depth;
        size_t peak_queue_depth;
        uint64_t bytes_sent;
        std::chrono::steady_clock::time_point last_send_time;
    };

    class AsyncSender
    {
    public:
        AsyncSender(std::shared_ptr<PriorityQueue> priority_queue,
                    std::shared_ptr<TcpConnection> tcp_connection);
        ~AsyncSender();

        // Lifecycle management
        void start();
        void stop();
        void shutdown(std::chrono::seconds timeout = std::chrono::seconds(5));

        // Status and monitoring
        bool isRunning() const;
        SenderStats getStats() const;
        size_t getQueueDepth() const;
        bool isConnected() const;

        // Configuration
        void setMaxRetries(size_t max_retries);
        void setBatchSize(size_t size);

        // Note: No sendAsync method - AsyncSender is a pure consumer
        // Messages are pushed directly to the priority queue by:
        // - gRPC handlers (for client orders)
        // - Market data processors (for market updates)
        // - Risk managers (for risk controls)
        // - Admin interfaces (for manual operations)

    private:
        using ErrorCallback = std::function<void(MessagePtr, const std::string &)>;
        ErrorCallback error_callback_;

        // Core components
        std::shared_ptr<PriorityQueue> priority_queue_;
        std::shared_ptr<TcpConnection> tcp_connection_;

        // Threading
        std::thread sender_thread_;
        std::atomic<bool> running_;
        std::atomic<bool> shutdown_requested_;

        // Performance tracking
        std::atomic<size_t> total_sent_{0};
        std::atomic<size_t> total_failed_{0};
        std::atomic<size_t> total_retried_{0};

        // Batch processing (Phase 3 optimization)
        bool enable_batch_processing_{false};
        size_t batch_size_{100};
        std::chrono::milliseconds batch_timeout_{1};

        // Configuration
        size_t max_retries_;
        std::chrono::milliseconds base_timeout_;

        // Private methods
        void senderLoop();
        void sendMessage(MessagePtr message);
        void handleSendFailure(MessagePtr message);
        std::chrono::milliseconds calculateTimeout() const;
        void updateStats(MessagePtr message, bool success);
        void resetStats();
    };

} // namespace fix_gateway::network