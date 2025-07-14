#include "utils/lockfree_priority_queue.h"
#include "utils/performance_timer.h"
#include "priority_config.h"

#include <iostream>
#include <sstream>
#include <cmath>

namespace fix_gateway::utils
{
    // AtomicRingBuffer Implementation
    LockFreePriorityQueue::AtomicRingBuffer::AtomicRingBuffer(size_t size)
        : capacity(size), mask(size - 1) // size must be power of 2
          ,
          messages(std::make_unique<std::atomic<fix_gateway::common::Message *>[]>(size))
    {
        // Verify size is power of 2
        if ((size & (size - 1)) != 0)
        {
            throw std::invalid_argument("Ring buffer size must be power of 2");
        }

        // Initialize all message slots to nullptr
        for (size_t i = 0; i < capacity; ++i)
        {
            messages[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    bool LockFreePriorityQueue::AtomicRingBuffer::push(MessagePtr message)
    {
        if (!message)
        {
            return false;
        }

        // Get current tail position
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) & mask;

        // Check if queue is full (would overwrite head)
        size_t current_head = head.load(std::memory_order_acquire);
        if (next_tail == current_head)
        {
            drop_count.fetch_add(1, std::memory_order_relaxed);
            return false; // Queue is full
        }

        // Store raw pointer (shared_ptr manages lifecycle)
        // We increase ref count by keeping shared_ptr alive in caller scope
        fix_gateway::common::Message *raw_ptr = message.get();
        messages[current_tail].store(raw_ptr, std::memory_order_release);

        // Advance tail pointer
        // Use compare_exchange to handle concurrent pushes
        while (!tail.compare_exchange_weak(current_tail, next_tail,
                                           std::memory_order_release,
                                           std::memory_order_relaxed))
        {
            // Retry with updated tail value
            next_tail = (current_tail + 1) & mask;
            current_head = head.load(std::memory_order_acquire);
            if (next_tail == current_head)
            {
                drop_count.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }

        push_count.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool LockFreePriorityQueue::AtomicRingBuffer::pop(MessagePtr &message)
    {
        // Get current head position
        size_t current_head = head.load(std::memory_order_relaxed);
        size_t current_tail = tail.load(std::memory_order_acquire);

        // Check if queue is empty
        if (current_head == current_tail)
        {
            return false; // Queue is empty
        }

        // Load message from current head position
        RawMessagePtr loaded_message = messages[current_head].load(std::memory_order_acquire);
        if (!loaded_message)
        {
            return false; // Race condition, message was consumed
        }

        // Clear the slot
        messages[current_head].store(nullptr, std::memory_order_release);

        // Advance head pointer
        size_t next_head = (current_head + 1) & mask;
        if (!head.compare_exchange_strong(current_head, next_head,
                                          std::memory_order_release,
                                          std::memory_order_relaxed))
        {
            // Another thread advanced head, restore message and retry
            messages[current_head].store(loaded_message, std::memory_order_release);
            return false;
        }

        message = MessagePtr(loaded_message, [](RawMessagePtr) {});
        pop_count.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    size_t LockFreePriorityQueue::AtomicRingBuffer::size() const
    {
        size_t current_tail = tail.load(std::memory_order_acquire);
        size_t current_head = head.load(std::memory_order_acquire);

        // Handle wrap-around
        if (current_tail >= current_head)
        {
            return current_tail - current_head;
        }
        else
        {
            return capacity - current_head + current_tail;
        }
    }

    bool LockFreePriorityQueue::AtomicRingBuffer::empty() const
    {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }

    // LockFreePriorityQueue Implementation
    LockFreePriorityQueue::LockFreePriorityQueue(
        size_t max_size_per_priority,
        const std::string &queue_name)
        : max_size_per_priority_(nextPowerOfTwo(max_size_per_priority)), queue_name_(queue_name), is_shutdown_(false)
    {
        // Initialize ring buffers for each priority level
        for (size_t i = 0; i < NUM_PRIORITIES; ++i)
        {
            priority_queues_[i] = std::make_unique<AtomicRingBuffer>(max_size_per_priority_);
        }

        std::cout << "[LOCKFREE] LockFreePriorityQueue '" << queue_name_
                  << "' created with " << max_size_per_priority_
                  << " capacity per priority (" << (max_size_per_priority_ * NUM_PRIORITIES)
                  << " total)" << std::endl;
    }

    LockFreePriorityQueue::~LockFreePriorityQueue()
    {
        shutdown();

        std::cout << "[LOCKFREE] LockFreePriorityQueue '" << queue_name_
                  << "' destroyed. Total processed: " << getTotalPopped()
                  << ", Dropped: " << getTotalDropped() << std::endl;
    }

    bool LockFreePriorityQueue::push(MessagePtr message)
    {
        if (is_shutdown_.load(std::memory_order_acquire) || !message)
        {
            return false;
        }

        Priority priority = message->getPriority();
        size_t index = priorityToIndex(priority);

        // Record timing for performance measurement
        PERF_TIMER_START(lockfree_push);

        bool success = priority_queues_[index]->push(message);

        PERF_TIMER_END(lockfree_push);

        return success;
    }

    bool LockFreePriorityQueue::tryPop(MessagePtr &message)
    {
        if (is_shutdown_.load(std::memory_order_acquire))
        {
            return false;
        }

        PERF_TIMER_START(lockfree_pop);

        // Check queues in priority order: CRITICAL -> HIGH -> MEDIUM -> LOW
        for (size_t i = 0; i < NUM_PRIORITIES; ++i)
        {
            if (priority_queues_[i]->pop(message))
            {
                PERF_TIMER_END(lockfree_pop);
                return true;
            }
        }

        PERF_TIMER_END(lockfree_pop);
        return false; // No messages in any queue
    }

    void LockFreePriorityQueue::shutdown()
    {
        is_shutdown_.store(true, std::memory_order_release);

        // Note: In lock-free implementation, we don't drain messages
        // They will be cleaned up by destructors when shared_ptr refcount reaches 0
    }

    bool LockFreePriorityQueue::isShutdown() const
    {
        return is_shutdown_.load(std::memory_order_acquire);
    }

    size_t LockFreePriorityQueue::size() const
    {
        size_t total = 0;
        for (const auto &queue : priority_queues_)
        {
            total += queue->size();
        }
        return total;
    }

    bool LockFreePriorityQueue::empty() const
    {
        for (const auto &queue : priority_queues_)
        {
            if (!queue->empty())
            {
                return false;
            }
        }
        return true;
    }

    size_t LockFreePriorityQueue::capacity() const
    {
        return max_size_per_priority_ * NUM_PRIORITIES;
    }

    uint64_t LockFreePriorityQueue::getTotalPushed() const
    {
        uint64_t total = 0;
        for (const auto &queue : priority_queues_)
        {
            total += queue->push_count.load(std::memory_order_relaxed);
        }
        return total;
    }

    uint64_t LockFreePriorityQueue::getTotalPopped() const
    {
        uint64_t total = 0;
        for (const auto &queue : priority_queues_)
        {
            total += queue->pop_count.load(std::memory_order_relaxed);
        }
        return total;
    }

    uint64_t LockFreePriorityQueue::getTotalDropped() const
    {
        uint64_t total = 0;
        for (const auto &queue : priority_queues_)
        {
            total += queue->drop_count.load(std::memory_order_relaxed);
        }
        return total;
    }

    size_t LockFreePriorityQueue::sizeByPriority(Priority priority) const
    {
        size_t index = priorityToIndex(priority);
        return priority_queues_[index]->size();
    }

    std::string LockFreePriorityQueue::toString() const
    {
        std::ostringstream oss;
        oss << "LockFreePriorityQueue{name=" << queue_name_
            << ", size=" << size()
            << ", capacity=" << capacity()
            << ", pushed=" << getTotalPushed()
            << ", popped=" << getTotalPopped()
            << ", dropped=" << getTotalDropped()
            << ", shutdown=" << (is_shutdown_.load() ? "true" : "false");

        // Per-priority breakdown
        oss << ", by_priority=[";
        for (size_t i = 0; i < NUM_PRIORITIES; ++i)
        {
            Priority p = indexToPriority(i);
            oss << "{" << static_cast<int>(p) << ":" << sizeByPriority(p) << "}";
            if (i < NUM_PRIORITIES - 1)
                oss << ",";
        }
        oss << "]}";

        return oss.str();
    }

    // Private helper methods
    size_t LockFreePriorityQueue::priorityToIndex(Priority priority) const noexcept
    {
        // CRITICAL=3 -> index 0 (highest priority, checked first)
        // HIGH=2     -> index 1
        // MEDIUM=1   -> index 2
        // LOW=0      -> index 3 (lowest priority, checked last)
        return static_cast<size_t>(Priority::CRITICAL) - static_cast<size_t>(priority);
    }

    Priority LockFreePriorityQueue::indexToPriority(size_t index) const noexcept
    {
        return static_cast<Priority>(static_cast<size_t>(Priority::CRITICAL) - index);
    }

    size_t LockFreePriorityQueue::nextPowerOfTwo(size_t n) const noexcept
    {
        if (n <= 1)
            return 2;

        // Use bit manipulation to find next power of 2
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        n++;

        return n;
    }

} // namespace fix_gateway::utils