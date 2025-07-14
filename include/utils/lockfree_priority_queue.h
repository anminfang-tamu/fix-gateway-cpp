#pragma once

#include "common/message.h"
#include "utils/performance_counters.h"

#include <atomic>
#include <array>
#include <memory>
#include <string>

namespace fix_gateway::utils
{
    using MessagePtr = fix_gateway::common::MessagePtr;
    using RawMessagePtr = fix_gateway::common::Message *;

    // Lock-free priority queue using atomic operations and ring buffers
    // Optimized for trading systems with predictable priority patterns
    class LockFreePriorityQueue
    {
        // Phase 3 Lock-Free Architecture:
        // - 4 separate atomic ring buffers (one per priority)
        // - Wait-free push/pop operations using atomic compare-and-swap
        // - CRITICAL messages always processed first
        // - No dynamic allocation in hot path
        // - Target: <50ns per operation

    public:
        // Constructor
        explicit LockFreePriorityQueue(
            size_t max_size_per_priority = 2048, // Power of 2 for efficient modulo
            const std::string &queue_name = "lockfree_priority_queue");

        // Destructor
        ~LockFreePriorityQueue();

        // Core operations (wait-free)
        bool push(MessagePtr message);
        bool tryPop(MessagePtr &message);

        // Queue management
        void shutdown();
        bool isShutdown() const;

        // Monitoring and statistics
        size_t size() const;
        bool empty() const;
        size_t capacity() const;

        // Performance metrics
        uint64_t getTotalPushed() const;
        uint64_t getTotalPopped() const;
        uint64_t getTotalDropped() const;

        // Priority-specific metrics
        size_t sizeByPriority(Priority priority) const;

        // Utility methods
        std::string toString() const;

    private:
        // Single priority queue implemented as atomic ring buffer
        struct AtomicRingBuffer
        {
            static constexpr size_t CACHE_LINE_SIZE = 64;

            // Align to cache line to prevent false sharing
            alignas(CACHE_LINE_SIZE) std::atomic<size_t> head{0};
            alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail{0};

            size_t capacity;
            size_t mask; // capacity - 1 for fast modulo (requires power of 2)

            // Message storage (aligned to prevent false sharing)
            // Use atomic raw pointers with manual reference counting for lock-free operations
            alignas(CACHE_LINE_SIZE) std::unique_ptr<std::atomic<RawMessagePtr>[]> messages;

            // Statistics
            alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> push_count{0};
            alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> pop_count{0};
            alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> drop_count{0};

            AtomicRingBuffer(size_t size);
            ~AtomicRingBuffer() = default;

            bool push(MessagePtr message);
            bool pop(MessagePtr &message);
            size_t size() const;
            bool empty() const;
        };

        static constexpr size_t NUM_PRIORITIES = 4;
        std::array<std::unique_ptr<AtomicRingBuffer>, NUM_PRIORITIES> priority_queues_;

        // Configuration
        size_t max_size_per_priority_;
        std::string queue_name_;

        // State management
        std::atomic<bool> is_shutdown_;

        // Helper methods
        size_t priorityToIndex(Priority priority) const noexcept;
        Priority indexToPriority(size_t index) const noexcept;
        size_t nextPowerOfTwo(size_t n) const noexcept;
    };

} // namespace fix_gateway::utils