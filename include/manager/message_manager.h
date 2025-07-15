#pragma once

#include "utils/priority_queue.h"
#include "network/tcp_connection.h"
#include "network/async_sender.h"
#include "common/message.h"
#include "priority_config.h"

#include <thread>
#include <memory>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <string>

namespace fix_gateway::manager
{
    using PriorityQueue = fix_gateway::utils::PriorityQueue;
    using TcpConnection = fix_gateway::network::TcpConnection;
    using MessagePtr = fix_gateway::common::MessagePtr;
    using AsyncSender = fix_gateway::network::AsyncSender;
    using SenderStats = fix_gateway::network::SenderStats;

    /**
     * @brief Message manager with core-pinned architecture for Phase 4
     *
     * Routes incoming messages to priority-specific queues and manages
     * dedicated AsyncSender threads pinned to individual CPU cores.
     * Creates and manages a shared TCP connection for all AsyncSenders.
     *
     * Architecture:
     * - Core 0: CRITICAL priority AsyncSender (< 10μs target)
     * - Core 1: HIGH priority AsyncSender (< 100μs target)
     * - Core 2: MEDIUM priority AsyncSender (< 1ms target)
     * - Core 3: LOW priority AsyncSender (< 10ms target)
     */
    class MessageManager
    {
    public:
        struct CorePinningConfig
        {
            // Core assignments
            int critical_core = 0; // Performance core for critical messages
            int high_core = 1;     // Performance core for high priority
            int medium_core = 2;   // Performance core for medium priority
            int low_core = 3;      // Performance core for low priority

            // Core pinning options
            bool enable_core_pinning = true;
            bool enable_real_time_priority = false; // Requires root on Linux/macOS

            // Queue sizes per priority
            size_t critical_queue_size = 1024;
            size_t high_queue_size = 2048;
            size_t medium_queue_size = 4096;
            size_t low_queue_size = 8192;
        };

        struct PerformanceStats
        {
            // Per-priority statistics
            std::unordered_map<Priority, SenderStats> priority_stats;

            // Core utilization
            std::unordered_map<int, double> core_utilization;

            // Cross-priority metrics
            uint64_t total_messages_sent = 0;
            uint64_t total_messages_failed = 0;
            double avg_critical_latency_ns = 0.0;

            // Priority distribution
            uint64_t critical_count = 0;
            uint64_t high_count = 0;
            uint64_t medium_count = 0;
            uint64_t low_count = 0;

            // TCP connection status
            bool tcp_connected = false;
            std::chrono::steady_clock::time_point last_connection_time;
        };

    public:
        explicit MessageManager(const CorePinningConfig &config);
        explicit MessageManager(); // Default constructor
        ~MessageManager();

        // Lifecycle management
        void start();
        void stop();
        void shutdown(std::chrono::seconds timeout = std::chrono::seconds(10));

        // Core message management functionality
        bool routeMessage(MessagePtr message);

        // Status and monitoring
        bool isRunning() const;
        bool isConnected() const;
        PerformanceStats getStats() const;
        SenderStats getStatsForPriority(Priority priority) const;

        // Configuration
        void setCoreAffinity(Priority priority, int core_id);
        void setRealTimePriority(Priority priority, bool enable);

        // Monitoring
        std::vector<size_t> getQueueDepths() const;
        size_t getQueueDepthForPriority(Priority priority) const;
        bool areAllCoresConnected() const;

        // Connection management
        bool connectToServer(const std::string &host, int port);
        void disconnectFromServer();

    private:
        // Core configuration
        CorePinningConfig config_;

        // Shared TCP connection (used by all AsyncSenders)
        std::shared_ptr<TcpConnection> tcp_connection_;

        // Per-priority queues and senders
        std::unordered_map<Priority, std::shared_ptr<PriorityQueue>> priority_queues_;
        std::unordered_map<Priority, std::unique_ptr<AsyncSender>> async_senders_;

        // Core management
        std::unordered_map<Priority, int> priority_to_core_;
        std::unordered_map<int, std::thread::id> core_to_thread_id_;

        // State management
        std::atomic<bool> running_;
        std::atomic<bool> shutdown_requested_;

        // Helper methods
        void createTcpConnection();
        void createQueuesAndSenders();
        void startAsyncSenders();
        void stopAsyncSenders();

        bool pinThreadToCore(std::thread &thread, int core_id);
        bool setThreadRealTimePriority(std::thread &thread);

        Priority getMessagePriority(MessagePtr message) const;
        std::shared_ptr<PriorityQueue> getQueueForPriority(Priority priority) const;

        size_t getQueueSizeForPriority(Priority priority) const;
        int getCoreForPriority(Priority priority) const;
    };

    /**
     * @brief Factory for creating optimized message managers
     */
    class MessageManagerFactory
    {
    public:
        // Pre-configured setups for different hardware
        static MessageManager::CorePinningConfig createM1MaxConfig();
        static MessageManager::CorePinningConfig createIntelConfig();
        static MessageManager::CorePinningConfig createDefaultConfig();

        // Trading environment specific configs
        static MessageManager::CorePinningConfig createLowLatencyConfig();
        static MessageManager::CorePinningConfig createHighThroughputConfig();

        // Hardware detection
        static int detectPerformanceCores();
        static std::vector<int> getOptimalCoreAssignment();
        static bool isRealTimePrioritySupported();
    };

} // namespace fix_gateway::manager