#pragma once

#include "common/message.h"
#include <atomic>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <string>
#include <sstream>
#include <mutex>

namespace fix_gateway::common
{
    template <typename T>
    class MessagePool
    {
    public:
        // Configuration constants
        static constexpr size_t DEFAULT_POOL_SIZE = 8192; // 8K pre-allocated messages
        static constexpr size_t CACHE_LINE_SIZE = 64;     // CPU cache line size

        // Minimal pool statistics for monitoring
        struct PoolStats
        {
            size_t total_capacity;
            size_t available_count;
            size_t allocated_count;
            uint64_t total_allocations;
            uint64_t total_deallocations;
            uint64_t allocation_failures;
        };

        // Constructor
        explicit MessagePool(size_t pool_size = DEFAULT_POOL_SIZE,
                             const std::string &pool_name = "message_pool");

        // Destructor
        ~MessagePool();

        // Non-copyable, non-movable
        MessagePool(const MessagePool &) = delete;
        MessagePool &operator=(const MessagePool &) = delete;
        MessagePool(MessagePool &&) = delete;
        MessagePool &operator=(MessagePool &&) = delete;

        // Core pool operations (lock-free O(1) - raw pointers for maximum performance)
        T *allocate();

        // Perfect forwarding constructor - allows any constructor arguments for type T
        template <typename... Args>
        T *allocate(Args &&...args);

        // Manual deallocation (required for raw pointer interface)
        void deallocate(T *msg);

        // Pool management
        void prewarm();  // Pre-touch memory pages
        void reset();    // Reset pool to initial state
        void shutdown(); // Shutdown pool operations

        // Status and monitoring
        size_t capacity() const { return pool_size_; }
        size_t available() const;
        size_t allocated() const;
        bool isEmpty() const { return available() == 0; }
        bool isFull() const { return allocated() == pool_size_; }
        PoolStats getStats() const;

        // Utility
        std::string toString() const;
        const std::string &getName() const { return pool_name_; }

    private:
        // Templated pool slot - generic message storage using aligned storage
        struct alignas(CACHE_LINE_SIZE) PoolSlot
        {
            // Use aligned storage to avoid construction until needed
            alignas(T) char message_storage[sizeof(T)];

            // Get typed pointer to storage
            T *get_message() { return reinterpret_cast<T *>(message_storage); }
            const T *get_message() const { return reinterpret_cast<const T *>(message_storage); }
        };

        // Pool configuration
        size_t pool_size_;
        std::string pool_name_;

        // Pool storage (cache-aligned array)
        std::unique_ptr<PoolSlot[]> pool_slots_;

        // Simple free list using slot indices (atomic stack)
        struct FreeListNode
        {
            std::atomic<int32_t> next_free_index{-1};
        };

        alignas(CACHE_LINE_SIZE) std::atomic<int32_t> free_list_head_{0};
        std::unique_ptr<FreeListNode[]> free_list_nodes_;

        // Essential statistics (cache-aligned for performance)
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> allocated_count_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> total_allocations_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> total_deallocations_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> allocation_failures_{0};

        // State
        std::atomic<bool> is_shutdown_{false};

        // Private methods
        T *allocateRaw();

        template <typename... Args>
        T *allocateWithArgs(Args &&...args);

        void deallocateRaw(T *msg);
        void initializeFreeList();
    };

    // Global templated message pool instance (singleton pattern) - same as your original design
    template <typename T>
    class GlobalMessagePool
    {
    public:
        static MessagePool<T> &getInstance(size_t pool_size = MessagePool<T>::DEFAULT_POOL_SIZE);

        static T *allocate();

        template <typename... Args>
        static T *allocate(Args &&...args);

        static void deallocate(T *msg);
        static void shutdown();

    private:
        static std::unique_ptr<MessagePool<T>> instance_;
        static std::atomic<bool> initialized_;
    };

    // Template implementation (must be in header for templates)

    template <typename T>
    MessagePool<T>::MessagePool(size_t pool_size, const std::string &pool_name)
        : pool_size_(pool_size), pool_name_(pool_name)
    {
        if (pool_size_ == 0)
        {
            throw std::invalid_argument("Pool size cannot be zero");
        }

        // Allocate pool slots and free list nodes
        pool_slots_ = std::make_unique<PoolSlot[]>(pool_size_);
        free_list_nodes_ = std::make_unique<FreeListNode[]>(pool_size_);

        // Initialize free list
        initializeFreeList();
    }

    template <typename T>
    MessagePool<T>::~MessagePool()
    {
        shutdown();

        // Note: We use aligned storage, so no automatic destructors are called
        // Objects must be properly deallocated before pool destruction
    }

    template <typename T>
    void MessagePool<T>::initializeFreeList()
    {
        // Build the free list by linking indices - same algorithm as original
        for (size_t i = 0; i < pool_size_ - 1; ++i)
        {
            free_list_nodes_[i].next_free_index.store(static_cast<int32_t>(i + 1), std::memory_order_relaxed);
        }
        // Last node points to -1 (end of list)
        free_list_nodes_[pool_size_ - 1].next_free_index.store(-1, std::memory_order_relaxed);

        // Head starts at index 0
        free_list_head_.store(0, std::memory_order_release);
        allocated_count_.store(0, std::memory_order_relaxed);
    }

    template <typename T>
    T *MessagePool<T>::allocate()
    {
        if (is_shutdown_.load(std::memory_order_acquire))
        {
            allocation_failures_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }

        return allocateRaw();
    }

    template <typename T>
    template <typename... Args>
    T *MessagePool<T>::allocate(Args &&...args)
    {
        if (is_shutdown_.load(std::memory_order_acquire))
        {
            allocation_failures_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }

        return allocateWithArgs(std::forward<Args>(args)...);
    }

    template <typename T>
    void MessagePool<T>::deallocate(T *msg)
    {
        if (msg)
        {
            // Call destructor explicitly since we used placement new
            msg->~T();
            deallocateRaw(msg);
        }
    }

    template <typename T>
    void MessagePool<T>::prewarm()
    {
        // Pre-touch all memory pages to avoid page faults during allocation - same as original
        for (size_t i = 0; i < pool_size_; ++i)
        {
            volatile char *touch_ptr = reinterpret_cast<volatile char *>(&pool_slots_[i]);
            *touch_ptr = 0;
        }
    }

    template <typename T>
    void MessagePool<T>::reset()
    {
        std::cout << "reset" << std::endl;
        // Caller responsibility: ensure pool is drained first
        if (allocated_count_.load() > 0)
        {
            std::cout << "Cannot reset non-empty pool" << std::endl;
            throw std::runtime_error("Cannot reset non-empty pool");
        }
        // Reinitialize free list - same as original
        initializeFreeList();
    }

    template <typename T>
    void MessagePool<T>::shutdown()
    {
        is_shutdown_.store(true, std::memory_order_release);
    }

    template <typename T>
    size_t MessagePool<T>::available() const
    {
        return pool_size_ - allocated_count_.load(std::memory_order_acquire);
    }

    template <typename T>
    size_t MessagePool<T>::allocated() const
    {
        return allocated_count_.load(std::memory_order_acquire);
    }

    template <typename T>
    typename MessagePool<T>::PoolStats MessagePool<T>::getStats() const
    {
        PoolStats stats;
        stats.total_capacity = pool_size_;
        stats.allocated_count = allocated();
        stats.available_count = available();
        stats.total_allocations = total_allocations_.load(std::memory_order_relaxed);
        stats.total_deallocations = total_deallocations_.load(std::memory_order_relaxed);
        stats.allocation_failures = allocation_failures_.load(std::memory_order_relaxed);
        return stats;
    }

    template <typename T>
    std::string MessagePool<T>::toString() const
    {
        auto stats = getStats();
        std::ostringstream oss;
        oss << "MessagePool<" << typeid(T).name() << ">{"
            << "name=" << pool_name_
            << ", capacity=" << stats.total_capacity
            << ", allocated=" << stats.allocated_count
            << ", available=" << stats.available_count
            << ", total_allocs=" << stats.total_allocations
            << ", total_deallocs=" << stats.total_deallocations
            << ", failures=" << stats.allocation_failures
            << ", utilization=" << (stats.allocated_count * 100.0 / stats.total_capacity) << "%"
            << "}";
        return oss.str();
    }

    // Private methods - Core lock-free algorithms (same as original)
    template <typename T>
    T *MessagePool<T>::allocateRaw()
    {
        // Lock-free pop from free list (atomic stack using indices) - exact same algorithm
        int32_t head_index = free_list_head_.load(std::memory_order_acquire);

        while (head_index >= 0)
        {
            int32_t next_index = free_list_nodes_[head_index].next_free_index.load(std::memory_order_relaxed);

            // Try to atomically update head to next
            if (free_list_head_.compare_exchange_weak(head_index, next_index,
                                                      std::memory_order_release,
                                                      std::memory_order_acquire))
            {
                // Successfully popped from free list
                allocated_count_.fetch_add(1, std::memory_order_relaxed);
                total_allocations_.fetch_add(1, std::memory_order_relaxed);

                // Use placement new with default constructor
                T *obj = pool_slots_[head_index].get_message();
                return new (obj) T();
            }
            // CAS failed, retry with updated head value
        }

        // Pool exhausted
        allocation_failures_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    template <typename T>
    template <typename... Args>
    T *MessagePool<T>::allocateWithArgs(Args &&...args)
    {
        // Lock-free pop from free list (atomic stack using indices) - same algorithm
        int32_t head_index = free_list_head_.load(std::memory_order_acquire);

        while (head_index >= 0)
        {
            int32_t next_index = free_list_nodes_[head_index].next_free_index.load(std::memory_order_relaxed);

            // Try to atomically update head to next
            if (free_list_head_.compare_exchange_weak(head_index, next_index,
                                                      std::memory_order_release,
                                                      std::memory_order_acquire))
            {
                // Successfully popped from free list
                allocated_count_.fetch_add(1, std::memory_order_relaxed);
                total_allocations_.fetch_add(1, std::memory_order_relaxed);

                // Use placement new with perfect forwarding
                T *obj = pool_slots_[head_index].get_message();
                return new (obj) T(std::forward<Args>(args)...);
            }
            // CAS failed, retry with updated head value
        }

        // Pool exhausted
        allocation_failures_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    template <typename T>
    void MessagePool<T>::deallocateRaw(T *msg)
    {
        if (!msg)
        {
            return;
        }

        // Convert message pointer back to slot index - same algorithm as original
        uintptr_t msg_addr = reinterpret_cast<uintptr_t>(msg);
        uintptr_t pool_start = reinterpret_cast<uintptr_t>(pool_slots_.get());
        uintptr_t pool_end = pool_start + (pool_size_ * sizeof(PoolSlot));

        if (msg_addr < pool_start || msg_addr >= pool_end)
        {
            return; // Message not from this pool
        }

        // Calculate slot index
        size_t slot_index = (msg_addr - pool_start) / sizeof(PoolSlot);
        if (slot_index >= pool_size_)
        {
            return;
        }

        // Verify this is the correct message
        if (msg != pool_slots_[slot_index].get_message())
        {
            return;
        }

        // Lock-free push to free list (atomic stack using indices) - same algorithm
        int32_t current_head = free_list_head_.load(std::memory_order_relaxed);
        int32_t slot_idx = static_cast<int32_t>(slot_index);

        do
        {
            free_list_nodes_[slot_index].next_free_index.store(current_head, std::memory_order_relaxed);
        } while (!free_list_head_.compare_exchange_weak(current_head, slot_idx,
                                                        std::memory_order_release,
                                                        std::memory_order_relaxed));

        // Update statistics
        allocated_count_.fetch_sub(1, std::memory_order_relaxed);
        total_deallocations_.fetch_add(1, std::memory_order_relaxed);
    }

    // Global instance implementations - same pattern as original
    template <typename T>
    std::unique_ptr<MessagePool<T>> GlobalMessagePool<T>::instance_ = nullptr;

    template <typename T>
    std::atomic<bool> GlobalMessagePool<T>::initialized_{false};

    template <typename T>
    MessagePool<T> &GlobalMessagePool<T>::getInstance(size_t pool_size)
    {
        if (!initialized_.load(std::memory_order_acquire))
        {
            // Double-checked locking pattern - same as original
            static std::mutex init_mutex;
            std::lock_guard<std::mutex> lock(init_mutex);

            if (!initialized_.load(std::memory_order_relaxed))
            {
                instance_ = std::make_unique<MessagePool<T>>(pool_size, "global_" + std::string(typeid(T).name()) + "_pool");
                instance_->prewarm();
                initialized_.store(true, std::memory_order_release);
            }
        }

        return *instance_;
    }

    template <typename T>
    T *GlobalMessagePool<T>::allocate()
    {
        return getInstance().allocate();
    }

    template <typename T>
    template <typename... Args>
    T *GlobalMessagePool<T>::allocate(Args &&...args)
    {
        return getInstance().allocate(std::forward<Args>(args)...);
    }

    template <typename T>
    void GlobalMessagePool<T>::deallocate(T *msg)
    {
        getInstance().deallocate(msg);
    }

    template <typename T>
    void GlobalMessagePool<T>::shutdown()
    {
        if (instance_)
        {
            instance_->shutdown();
            instance_.reset();
            initialized_.store(false, std::memory_order_release);
        }
    }

    // Backward compatibility - your existing code will work unchanged!
    using LegacyMessagePool = MessagePool<Message>;
    using LegacyGlobalMessagePool = GlobalMessagePool<Message>;

    // Convenience factory functions - same as your original design
    namespace pool
    {
        // Create message using global pool - works with any type T
        template <typename T, typename... Args>
        inline T *createMessage(Args &&...args)
        {
            return GlobalMessagePool<T>::allocate(std::forward<Args>(args)...);
        }

        // Create message with custom pool - works with any type T
        template <typename T, typename... Args>
        inline T *createMessage(MessagePool<T> &pool, Args &&...args)
        {
            return pool.allocate(std::forward<Args>(args)...);
        }

        // Deallocate message using global pool - works with any type T
        template <typename T>
        inline void deallocateMessage(T *msg)
        {
            GlobalMessagePool<T>::deallocate(msg);
        }
    }

} // namespace fix_gateway::common