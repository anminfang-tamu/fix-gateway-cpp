#pragma once

#include "utils/lockfree_queue.h"
#include "protocol/fix_message.h"
#include "../application/priority_queue_container.h"
#include "../../config/priority_config.h"

#include <atomic>
#include <memory>
#include <chrono>
#include <array>

namespace fix_gateway::manager
{
    using FixMessage = fix_gateway::protocol::FixMessage;
    using FixMessageQueue = fix_gateway::utils::LockFreeQueue<FixMessage *>;

    // High-performance router statistics (lock-free)
    struct RouterStats
    {
        alignas(64) std::atomic<uint64_t> messages_routed{0};
        alignas(64) std::atomic<uint64_t> messages_dropped{0};
        alignas(64) std::atomic<uint64_t> routing_errors{0};
        alignas(64) std::atomic<uint64_t> total_routing_time_ns{0};
        alignas(64) std::atomic<uint64_t> peak_routing_time_ns{0};
        
        // Per-priority routing counts
        alignas(64) std::atomic<uint64_t> critical_routed{0};
        alignas(64) std::atomic<uint64_t> high_routed{0};
        alignas(64) std::atomic<uint64_t> medium_routed{0};
        alignas(64) std::atomic<uint64_t> low_routed{0};
        
        // Per-priority drop counts
        alignas(64) std::atomic<uint64_t> critical_dropped{0};
        alignas(64) std::atomic<uint64_t> high_dropped{0};
        alignas(64) std::atomic<uint64_t> medium_dropped{0};
        alignas(64) std::atomic<uint64_t> low_dropped{0};
    };

    class MessageRouter
    {
    public:
        MessageRouter(std::shared_ptr<PriorityQueueContainer> queues);
        ~MessageRouter();

        // lifecycle
        void start();
        void stop();

        // OPTIMIZED: Zero-copy routing with perfect forwarding
        // Target: < 50ns routing latency for critical path
        void routeMessage(FixMessage *message) noexcept;
        
        // OPTIMIZED: Batch routing for high throughput scenarios
        void routeMessages(FixMessage **messages, size_t count) noexcept;
        
        // OPTIMIZED: Move semantics for pointer transfer
        template<typename MessagePtr>
        inline void routeMessageMove(MessagePtr &&message) noexcept;

        // monitoring
        bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }
        const RouterStats& getStats() const noexcept { return stats_; }
        void resetStats() noexcept;
        
        // Performance metrics
        double getAverageRoutingLatencyNs() const noexcept;
        uint64_t getPeakRoutingLatencyNs() const noexcept;
        uint64_t getMessagesRoutedPerSecond() const noexcept;

    private:
        // OPTIMIZED: Inline hot path methods (no function call overhead)
        Priority getMessagePriority(const FixMessage *message) const noexcept;
        constexpr int getPriorityIndex(Priority priority) const noexcept;
        
        // OPTIMIZED: Branch prediction hints for common cases
        bool tryRouteToQueue(FixMessage *message, Priority priority) noexcept;
        
        // OPTIMIZED: Lock-free error tracking (no logging in hot path)
        inline void recordRoutingSuccess(Priority priority, uint64_t routing_time_ns) noexcept;
        inline void recordRoutingFailure(Priority priority) noexcept;
        
        // Performance monitoring
        inline void updateRoutingStats(Priority priority, uint64_t start_time_ns) noexcept;

        // infrastructure - shared priority queues
        std::shared_ptr<PriorityQueueContainer> queues_;
        std::atomic<bool> running_;
        
        // OPTIMIZED: Cache-aligned performance statistics
        mutable RouterStats stats_;
        
        // OPTIMIZED: Pre-calculated priority to index mapping (compile-time constant)
        static constexpr std::array<int, 4> PRIORITY_TO_INDEX = {
            static_cast<int>(Priority::CRITICAL),  // 0
            static_cast<int>(Priority::HIGH),      // 1  
            static_cast<int>(Priority::MEDIUM),    // 2
            static_cast<int>(Priority::LOW)        // 3
        };
    };

    // OPTIMIZED: Template specialization for perfect forwarding
    template<typename MessagePtr>
    inline void MessageRouter::routeMessageMove(MessagePtr &&message) noexcept
    {
        static_assert(std::is_pointer_v<std::decay_t<MessagePtr>>, 
                      "MessagePtr must be a pointer type");
        
        // Perfect forwarding preserves value category
        routeMessage(std::forward<MessagePtr>(message));
    }

} // namespace fix_gateway::manager