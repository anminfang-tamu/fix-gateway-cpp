#include "common/message_pool.h"
#include "utils/logger.h"
#include <iostream>
#include <sstream>
#include <mutex>

namespace fix_gateway::common
{
    using namespace fix_gateway::utils;

    // MessagePool implementation
    MessagePool::MessagePool(size_t pool_size, const std::string &pool_name)
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

        LOG(LogLevel::INFO) << "[POOL] MessagePool '" << pool_name_ << "' created with "
                            << pool_size_ << " pre-allocated messages ("
                            << (pool_size_ * sizeof(PoolSlot)) / 1024 << " KB)";
    }

    MessagePool::~MessagePool()
    {
        shutdown();

        LOG(LogLevel::INFO) << "[POOL] MessagePool '" << pool_name_ << "' destroyed. "
                            << "Total allocations: " << total_allocations_.load()
                            << ", Total deallocations: " << total_deallocations_.load()
                            << ", Failures: " << allocation_failures_.load();
    }

    void MessagePool::initializeFreeList()
    {
        // Build the free list by linking indices
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

    Message *MessagePool::allocate()
    {
        if (is_shutdown_.load(std::memory_order_acquire))
        {
            allocation_failures_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }

        return allocateRaw();
    }

    Message *MessagePool::allocate(const std::string &message_id,
                                   const std::string &payload,
                                   Priority priority,
                                   MessageType message_type,
                                   const std::string &session_id,
                                   const std::string &destination)
    {
        Message *msg = allocate();
        if (!msg)
        {
            return nullptr;
        }

        // Reuse existing message object (more efficient than placement new)
        new (msg) Message(message_id, payload, priority, message_type, session_id, destination);

        return msg;
    }

    void MessagePool::deallocate(Message *msg)
    {
        deallocateRaw(msg);
    }

    void MessagePool::prewarm()
    {
        LOG(LogLevel::INFO) << "[POOL] Prewarming MessagePool '" << pool_name_ << "'...";

        // Pre-touch all memory pages to avoid page faults during allocation
        for (size_t i = 0; i < pool_size_; ++i)
        {
            volatile char *touch_ptr = reinterpret_cast<volatile char *>(&pool_slots_[i]);
            *touch_ptr = 0;
        }

        LOG(LogLevel::INFO) << "[POOL] MessagePool '" << pool_name_ << "' prewarmed successfully";
    }

    void MessagePool::reset()
    {
        LOG(LogLevel::INFO) << "[POOL] Resetting MessagePool '" << pool_name_ << "'...";

        // Reinitialize free list
        initializeFreeList();

        LOG(LogLevel::INFO) << "[POOL] MessagePool '" << pool_name_ << "' reset complete";
    }

    void MessagePool::shutdown()
    {
        is_shutdown_.store(true, std::memory_order_release);
        LOG(LogLevel::INFO) << "[POOL] MessagePool '" << pool_name_ << "' shutdown initiated";
    }

    size_t MessagePool::available() const
    {
        return pool_size_ - allocated_count_.load(std::memory_order_acquire);
    }

    size_t MessagePool::allocated() const
    {
        return allocated_count_.load(std::memory_order_acquire);
    }

    MessagePool::PoolStats MessagePool::getStats() const
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

    std::string MessagePool::toString() const
    {
        auto stats = getStats();
        std::ostringstream oss;
        oss << "MessagePool{name=" << pool_name_
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

    // Private methods - Core lock-free algorithms
    Message *MessagePool::allocateRaw()
    {
        // Lock-free pop from free list (atomic stack using indices)
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

                return &pool_slots_[head_index].message_storage;
            }
            // CAS failed, retry with updated head value
        }

        // Pool exhausted
        return nullptr;
    }

    void MessagePool::deallocateRaw(Message *msg)
    {
        if (!msg)
        {
            return;
        }

        // Convert message pointer back to slot index
        uintptr_t msg_addr = reinterpret_cast<uintptr_t>(msg);
        uintptr_t pool_start = reinterpret_cast<uintptr_t>(pool_slots_.get());
        uintptr_t pool_end = pool_start + (pool_size_ * sizeof(PoolSlot));

        if (msg_addr < pool_start || msg_addr >= pool_end)
        {
            LOG(LogLevel::ERROR) << "[POOL] ERROR: Attempting to deallocate message not from pool '"
                                 << pool_name_ << "'";
            return;
        }

        // Calculate slot index
        size_t slot_index = (msg_addr - pool_start) / sizeof(PoolSlot);
        if (slot_index >= pool_size_)
        {
            LOG(LogLevel::ERROR) << "[POOL] ERROR: Invalid slot index " << slot_index;
            return;
        }

        // Verify this is the correct message
        if (msg != &pool_slots_[slot_index].message_storage)
        {
            LOG(LogLevel::ERROR) << "[POOL] ERROR: Message pointer mismatch";
            return;
        }

        // No need to call destructor - we'll reuse the object

        // Lock-free push to free list (atomic stack using indices)
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

    // GlobalMessagePool implementation
    std::unique_ptr<MessagePool> GlobalMessagePool::instance_ = nullptr;
    std::atomic<bool> GlobalMessagePool::initialized_{false};

    MessagePool &GlobalMessagePool::getInstance(size_t pool_size)
    {
        if (!initialized_.load(std::memory_order_acquire))
        {
            // Double-checked locking pattern
            static std::mutex init_mutex;
            std::lock_guard<std::mutex> lock(init_mutex);

            if (!initialized_.load(std::memory_order_relaxed))
            {
                instance_ = std::make_unique<MessagePool>(pool_size, "global_message_pool");
                instance_->prewarm();
                initialized_.store(true, std::memory_order_release);
            }
        }

        return *instance_;
    }

    Message *GlobalMessagePool::allocate()
    {
        return getInstance().allocate();
    }

    Message *GlobalMessagePool::allocate(const std::string &message_id,
                                         const std::string &payload,
                                         Priority priority,
                                         MessageType message_type,
                                         const std::string &session_id,
                                         const std::string &destination)
    {
        return getInstance().allocate(message_id, payload, priority, message_type, session_id, destination);
    }

    void GlobalMessagePool::deallocate(Message *msg)
    {
        getInstance().deallocate(msg);
    }

    void GlobalMessagePool::shutdown()
    {
        if (instance_)
        {
            instance_->shutdown();
            instance_.reset();
            initialized_.store(false, std::memory_order_release);
        }
    }

} // namespace fix_gateway::common