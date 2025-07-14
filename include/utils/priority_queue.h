#pragma once

#include "common/message.h"
#include "utils/performance_counters.h"

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>

namespace fix_gateway::utils
{
    using MessagePtr = fix_gateway::common::MessagePtr;
    using MessagePriorityComparator = fix_gateway::common::MessagePriorityComparator;

    // Queue overflow policies for when queue reaches capacity
    enum class OverflowPolicy
    {
        DROP_OLDEST, // Remove oldest message to make room
        DROP_NEWEST, // Drop the incoming message
        BLOCK,       // Block until space is available
        REJECT       // Return false immediately
    };

    // Queue statistics for monitoring
    struct QueueStats
    {
        size_t current_size;
        size_t max_size;
        uint64_t total_pushed;
        uint64_t total_popped;
        uint64_t total_dropped;
        uint64_t peak_size;
        double avg_latency_ns;
        std::string overflow_policy_str;
    };

    class PriorityQueue
    {
        // TODO: Phase 3 Lock-Free Migration Plan:
        // 1. Replace STL priority_queue with boost::lockfree::priority_queue
        // 2. Replace mutex with lock-free atomics for all operations
        // 3. Replace condition_variable with lock-free wait/notify mechanisms
        // 4. Use memory ordering for cross-thread synchronization
        // 5. Target: <100ns per queue operation for sub-10μs latency

    public:
        // Constructor
        explicit PriorityQueue(
            size_t max_size = 10000,
            OverflowPolicy overflow_policy = OverflowPolicy::DROP_OLDEST,
            const std::string &queue_name = "priority_queue");

        // Destructor
        ~PriorityQueue();

        // Core operations
        bool push(MessagePtr message);
        bool push(MessagePtr message, std::chrono::milliseconds timeout);

        // Blocking pop - waits for message
        bool pop(MessagePtr &message);
        bool pop(MessagePtr &message, std::chrono::milliseconds timeout);

        // Non-blocking pop - returns immediately
        bool tryPop(MessagePtr &message);

        // Queue management
        void clear();
        void shutdown();
        bool isShutdown() const;

        // Monitoring and statistics
        size_t size() const;
        bool empty() const;
        size_t capacity() const;
        QueueStats getStats() const;

        // Performance metrics
        uint64_t getTotalPushed() const;
        uint64_t getTotalPopped() const;
        uint64_t getTotalDropped() const;
        size_t getPeakSize() const;
        double getAverageLatency() const;

        // Configuration
        void setOverflowPolicy(OverflowPolicy policy);
        OverflowPolicy getOverflowPolicy() const;
        void setMaxSize(size_t max_size);

        // Utility methods
        std::string toString() const;
        std::string getOverflowPolicyString() const;

    private:
        // Core queue data structure
        std::priority_queue<MessagePtr, std::vector<MessagePtr>, MessagePriorityComparator> queue_;

        // Thread synchronization
        mutable std::mutex mutex_;
        std::condition_variable not_empty_cv_;
        std::condition_variable not_full_cv_;

        // Configuration
        size_t max_size_;
        OverflowPolicy overflow_policy_;
        std::string queue_name_;

        // State management
        std::atomic<bool> is_shutdown_;

        // Performance tracking
        std::atomic<uint64_t> total_pushed_;
        std::atomic<uint64_t> total_popped_;
        std::atomic<uint64_t> total_dropped_;
        std::atomic<size_t> peak_size_;

        // Latency tracking
        std::atomic<uint64_t> total_latency_ns_;
        std::atomic<uint64_t> latency_samples_;

        // Helper methods
        bool pushInternal(MessagePtr message);
        bool handleOverflow(MessagePtr message);
        void updateStats(MessagePtr message);
        void recordLatency(const std::chrono::steady_clock::time_point &start_time);
        std::string formatStats() const;
    };

    // Utility functions
    std::string overflowPolicyToString(OverflowPolicy policy);
    OverflowPolicy stringToOverflowPolicy(const std::string &policy_str);

} // namespace fix_gateway::utils