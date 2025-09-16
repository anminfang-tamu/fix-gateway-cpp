#pragma once

#include "common/message.h"
#include "utils/performance_counters.h"

#include <atomic>
#include <array>
#include <memory>
#include <string>

// Forward declaration to avoid circular dependency
namespace fix_gateway::protocol
{
    class FixMessage;
}

namespace fix_gateway::utils
{
    using FixMessage = fix_gateway::protocol::FixMessage;
    using MessagePtr = fix_gateway::common::MessagePtr;
    using RawMessagePtr = fix_gateway::common::Message *;

    // Simple lock-free queue using atomic operations and ring buffer
    // Optimized for trading systems - no priority logic, just fast FIFO
    template <typename T>
    class LockFreeQueue
    {
    public:
        // Constructor
        explicit LockFreeQueue(
            size_t max_size = 2048, // Power of 2 for efficient modulo
            const std::string &queue_name = "lockfree_queue");

        // Destructor
        ~LockFreeQueue() = default;

        // Core operations (wait-free)
        bool push(T message);
        bool tryPop(T &message);

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

        // Utility methods
        std::string toString() const;

    private:
        // Atomic ring buffer implementation
        static constexpr size_t CACHE_LINE_SIZE = 64;

        // Align to cache line to prevent false sharing
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};

        size_t capacity_;
        size_t mask_; // capacity - 1 for fast modulo (requires power of 2)

        // Message storage (aligned to prevent false sharing)
        // For non-trivially copyable types like shared_ptr, store them normally
        alignas(CACHE_LINE_SIZE) std::unique_ptr<T[]> messages_;

        // Statistics
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> push_count_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> pop_count_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> drop_count_{0};

        // Configuration
        std::string queue_name_;

        // State management
        std::atomic<bool> is_shutdown_;

        // Helper methods
        size_t nextPowerOfTwo(size_t n) const noexcept;
    };

    // Type aliases for convenience
    using FixMessageQueue = LockFreeQueue<FixMessage *>;
    using GenericMessageQueue = LockFreeQueue<MessagePtr>;

    // Template implementation for LockFreeQueue (header-only)
    template <typename T>
    LockFreeQueue<T>::LockFreeQueue(size_t max_size, const std::string &queue_name)
        : capacity_(nextPowerOfTwo(max_size)), mask_(capacity_ - 1), queue_name_(queue_name), is_shutdown_(false)
    {
        messages_ = std::make_unique<T[]>(capacity_);
        // Initialize with default values
        for (size_t i = 0; i < capacity_; ++i)
        {
            messages_[i] = T{};
        }
    }

    template <typename T>
    bool LockFreeQueue<T>::push(T message)
    {
        if (is_shutdown_.load(std::memory_order_acquire))
            return false;

        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) & mask_;

        if (next_tail == head_.load(std::memory_order_acquire))
        {
            drop_count_.fetch_add(1, std::memory_order_relaxed);
            return false; // Queue full
        }

        messages_[current_tail] = message;
        tail_.store(next_tail, std::memory_order_release);
        push_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    template <typename T>
    bool LockFreeQueue<T>::tryPop(T &message)
    {
        if (is_shutdown_.load(std::memory_order_acquire))
        {
            message = T{};
            return false;
        }

        size_t current_head = head_.load(std::memory_order_relaxed);

        if (current_head == tail_.load(std::memory_order_acquire))
        {
            message = T{};
            return false; // Queue empty
        }

        message = messages_[current_head];
        head_.store((current_head + 1) & mask_, std::memory_order_release);
        pop_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    template <typename T>
    void LockFreeQueue<T>::shutdown()
    {
        is_shutdown_.store(true, std::memory_order_release);
    }

    template <typename T>
    bool LockFreeQueue<T>::isShutdown() const
    {
        return is_shutdown_.load(std::memory_order_acquire);
    }

    template <typename T>
    size_t LockFreeQueue<T>::size() const
    {
        return (tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire)) & mask_;
    }

    template <typename T>
    bool LockFreeQueue<T>::empty() const
    {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    template <typename T>
    size_t LockFreeQueue<T>::capacity() const
    {
        return capacity_;
    }

    template <typename T>
    uint64_t LockFreeQueue<T>::getTotalPushed() const
    {
        return push_count_.load(std::memory_order_acquire);
    }

    template <typename T>
    uint64_t LockFreeQueue<T>::getTotalPopped() const
    {
        return pop_count_.load(std::memory_order_acquire);
    }

    template <typename T>
    uint64_t LockFreeQueue<T>::getTotalDropped() const
    {
        return drop_count_.load(std::memory_order_acquire);
    }

    template <typename T>
    std::string LockFreeQueue<T>::toString() const
    {
        return queue_name_ + " [size: " + std::to_string(size()) +
               ", capacity: " + std::to_string(capacity_) +
               ", pushed: " + std::to_string(getTotalPushed()) +
               ", popped: " + std::to_string(getTotalPopped()) +
               ", dropped: " + std::to_string(getTotalDropped()) + "]";
    }

    template <typename T>
    size_t LockFreeQueue<T>::nextPowerOfTwo(size_t n) const noexcept
    {
        if (n == 0)
            return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }
} // namespace fix_gateway::utils
