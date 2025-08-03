#pragma once

#include "utils/priority_queue.h"
#include "utils/lockfree_queue.h"
#include "network/tcp_connection.h"
#include "network/async_sender.h"
#include "common/message.h"
#include "priority_config.h"

#include <thread>
#include <memory>
#include <atomic>
#include <vector>
#include <string>

namespace fix_gateway::manager
{
    using MessagePtr = fix_gateway::common::MessagePtr;
    using PriorityQueue = fix_gateway::utils::PriorityQueue;
    using LockFreeQueue = fix_gateway::utils::LockFreeQueue<MessagePtr>;
    using TcpConnection = fix_gateway::network::TcpConnection;
    using AsyncSender = fix_gateway::network::AsyncSender;
    using SenderStats = fix_gateway::network::SenderStats;

    /**
     * @brief High-performance AsyncSender manager with priority-based core pinning
     *
     * Manages 4 separate AsyncSender instances, each pinned to dedicated CPU cores
     * for optimal priority-based message processing without interference.
     *
     * Architecture:
     * - Core 0: CRITICAL priority AsyncSender (< 10μs target) - BusinessLogicManager
     * - Core 1: HIGH priority AsyncSender (< 100μs target) - BusinessLogicManager
     * - Core 2: MEDIUM priority AsyncSender (< 1ms target) - BusinessLogicManager
     * - Core 3: LOW priority AsyncSender (< 10ms target) - FixSessionManager
     *
     * Each AsyncSender monitors its own priority queue with dedicated core resources.
     */
    class AsyncSenderManager
    {
    public:
        enum class QueueType
        {
            MUTEX_BASED, // Phase 2: STL priority_queue with mutex
            LOCK_FREE    // Phase 3: Lock-free ring buffer implementation
        };

        struct CorePinningConfig
        {
            // Core assignments for priority-specific AsyncSenders
            int critical_core = 0; // Performance core for critical messages
            int high_core = 1;     // Performance core for high priority
            int medium_core = 2;   // Performance core for medium priority
            int low_core = 3;      // Performance core for low priority

            // Core pinning options
            bool enable_core_pinning = true;
            bool enable_real_time_priority = false; // Requires root on Linux/macOS

            // Queue configuration (Phase 3)
            QueueType queue_type = QueueType::MUTEX_BASED; // Default to mutex-based for compatibility

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

            // Queue type info
            QueueType queue_type;
            std::string queue_type_string;
        };

    public:
        explicit AsyncSenderManager(const CorePinningConfig &config);
        explicit AsyncSenderManager(); // Default constructor
        ~AsyncSenderManager();

        // Lifecycle management
        void start();
        void stop();
        void shutdown(std::chrono::seconds timeout = std::chrono::seconds(10));

        // Core AsyncSender access - used by InboundMessageManager implementations
        std::shared_ptr<AsyncSender> getAsyncSenderForPriority(Priority priority) const;
        std::shared_ptr<AsyncSender> getCriticalSender() const; // For BusinessLogicManager
        std::shared_ptr<AsyncSender> getHighSender() const;     // For BusinessLogicManager
        std::shared_ptr<AsyncSender> getMediumSender() const;   // For BusinessLogicManager
        std::shared_ptr<AsyncSender> getLowSender() const;      // For FixSessionManager

        // Direct message sending (routes to appropriate priority queue)
        bool sendMessage(MessagePtr message, Priority priority);

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

        // Queue type info
        QueueType getQueueType() const { return config_.queue_type; }
        std::string getQueueTypeString() const;

    private:
        // Core configuration
        CorePinningConfig config_;

        // Shared TCP connection (used by all AsyncSenders)
        std::shared_ptr<TcpConnection> tcp_connection_;

        // Per-priority queues and senders - supports both mutex-based and lock-free
        std::unordered_map<Priority, std::shared_ptr<PriorityQueue>> priority_queues_;
        std::unordered_map<Priority, std::shared_ptr<LockFreeQueue>> lockfree_queues_;
        std::unordered_map<Priority, std::shared_ptr<AsyncSender>> async_senders_;

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

        // Thread management helpers (preserved core performance logic)
        bool pinThreadToCore(std::thread &thread, int core_id);
        bool setThreadQoSClass(std::thread &thread, int core_id);
        bool setThreadRealTimePriority(std::thread &thread);

        // Queue interface abstraction
        bool pushToQueue(Priority priority, MessagePtr message);
        size_t getQueueSize(Priority priority) const;

        size_t getQueueSizeForPriority(Priority priority) const;
        int getCoreForPriority(Priority priority) const;
    };

    /**
     * @brief Factory for creating optimized AsyncSender managers
     */
    class AsyncSenderManagerFactory
    {
    public:
        // Pre-configured setups for different hardware
        static AsyncSenderManager::CorePinningConfig createM1MaxConfig();
        static AsyncSenderManager::CorePinningConfig createIntelConfig();
        static AsyncSenderManager::CorePinningConfig createDefaultConfig();

        // Trading environment specific configs
        static AsyncSenderManager::CorePinningConfig createLowLatencyConfig();
        static AsyncSenderManager::CorePinningConfig createHighThroughputConfig();

        // Phase 3: Lock-free configurations
        static AsyncSenderManager::CorePinningConfig createLockFreeConfig();
        static AsyncSenderManager::CorePinningConfig createLockFreeM1MaxConfig();

        // Hardware detection
        static int detectPerformanceCores();
        static std::vector<int> getOptimalCoreAssignment();
        static bool isRealTimePrioritySupported();
    };

} // namespace fix_gateway::manager