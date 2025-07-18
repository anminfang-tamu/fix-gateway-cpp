#pragma once

#include "common/message.h"
#include <atomic>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fix_gateway::common
{
    // High-performance lock-free message pool for trading systems
    // Uses raw pointers and atomic free list for O(1) allocation/deallocation
    // Target: <100ns allocation latency (eliminates shared_ptr overhead)
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
        Message *allocate();
        Message *allocate(const std::string &message_id,
                          const std::string &payload,
                          Priority priority = Priority::LOW,
                          MessageType message_type = MessageType::UNKNOWN,
                          const std::string &session_id = "",
                          const std::string &destination = "");

        // Manual deallocation (required for raw pointer interface)
        void deallocate(Message *msg);

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
        // Simplified pool slot - just message storage
        struct alignas(CACHE_LINE_SIZE) PoolSlot
        {
            Message message_storage;

            // Constructor for placement new
            PoolSlot() : message_storage("", "", Priority::LOW, MessageType::UNKNOWN, "", "") {}
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
        Message *allocateRaw();
        void deallocateRaw(Message *msg);
        void initializeFreeList();
    };

    // Global message pool instance (singleton pattern) - raw pointer interface
    class GlobalMessagePool
    {
    public:
        static MessagePool &getInstance(size_t pool_size = MessagePool::DEFAULT_POOL_SIZE);
        static Message *allocate();
        static Message *allocate(const std::string &message_id,
                                 const std::string &payload,
                                 Priority priority = Priority::LOW,
                                 MessageType message_type = MessageType::UNKNOWN,
                                 const std::string &session_id = "",
                                 const std::string &destination = "");
        static void deallocate(Message *msg);
        static void shutdown();

    private:
        static std::unique_ptr<MessagePool> instance_;
        static std::atomic<bool> initialized_;
    };

    // Convenience factory functions - raw pointer interface
    namespace pool
    {
        // Create message using global pool (raw pointer - caller must deallocate)
        inline Message *createMessage(const std::string &message_id,
                                      const std::string &payload,
                                      Priority priority = Priority::LOW,
                                      MessageType message_type = MessageType::UNKNOWN,
                                      const std::string &session_id = "",
                                      const std::string &destination = "")
        {
            return GlobalMessagePool::allocate(message_id, payload, priority, message_type, session_id, destination);
        }

        // Create message with custom pool (raw pointer - caller must deallocate)
        inline Message *createMessage(MessagePool &pool,
                                      const std::string &message_id,
                                      const std::string &payload,
                                      Priority priority = Priority::LOW,
                                      MessageType message_type = MessageType::UNKNOWN,
                                      const std::string &session_id = "",
                                      const std::string &destination = "")
        {
            return pool.allocate(message_id, payload, priority, message_type, session_id, destination);
        }

        // Deallocate message using global pool
        inline void deallocateMessage(Message *msg)
        {
            GlobalMessagePool::deallocate(msg);
        }
    }

} // namespace fix_gateway::common