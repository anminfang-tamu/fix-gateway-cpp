#include "manager/message_router.h"
#include "protocol/fix_fields.h"

#include <chrono>

// Platform-specific prefetch hints
#ifdef __x86_64__
    #include <immintrin.h>
    #define PREFETCH(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
#elif defined(__aarch64__) || defined(__arm__)
    #define PREFETCH(addr) __builtin_prefetch(addr, 0, 3)
#else
    #define PREFETCH(addr) // No-op on unsupported platforms
#endif

namespace fix_gateway::manager
{
    using namespace fix_gateway::protocol;

    MessageRouter::MessageRouter(std::shared_ptr<PriorityQueueContainer> queues)
        : running_(false), queues_(queues), stats_{}
    {
    }

    MessageRouter::~MessageRouter()
    {
        stop();
    }

    void MessageRouter::start()
    {
        if (running_.load(std::memory_order_acquire))
        {
            return; // Already running
        }

        running_.store(true, std::memory_order_release);
    }

    void MessageRouter::stop()
    {
        running_.store(false, std::memory_order_release);
    }

    void MessageRouter::resetStats() noexcept
    {
        stats_.messages_routed.store(0, std::memory_order_relaxed);
        stats_.messages_dropped.store(0, std::memory_order_relaxed);
        stats_.routing_errors.store(0, std::memory_order_relaxed);
        stats_.total_routing_time_ns.store(0, std::memory_order_relaxed);
        stats_.peak_routing_time_ns.store(0, std::memory_order_relaxed);
        
        stats_.critical_routed.store(0, std::memory_order_relaxed);
        stats_.high_routed.store(0, std::memory_order_relaxed);
        stats_.medium_routed.store(0, std::memory_order_relaxed);
        stats_.low_routed.store(0, std::memory_order_relaxed);
        
        stats_.critical_dropped.store(0, std::memory_order_relaxed);
        stats_.high_dropped.store(0, std::memory_order_relaxed);
        stats_.medium_dropped.store(0, std::memory_order_relaxed);
        stats_.low_dropped.store(0, std::memory_order_relaxed);
    }

    double MessageRouter::getAverageRoutingLatencyNs() const noexcept
    {
        uint64_t total_messages = stats_.messages_routed.load(std::memory_order_relaxed);
        if (total_messages == 0) return 0.0;
        
        uint64_t total_time = stats_.total_routing_time_ns.load(std::memory_order_relaxed);
        return static_cast<double>(total_time) / static_cast<double>(total_messages);
    }

    uint64_t MessageRouter::getPeakRoutingLatencyNs() const noexcept
    {
        return stats_.peak_routing_time_ns.load(std::memory_order_relaxed);
    }

    uint64_t MessageRouter::getMessagesRoutedPerSecond() const noexcept
    {
        // This would need a time window calculation, simplified for now
        return stats_.messages_routed.load(std::memory_order_relaxed);
    }

    // OPTIMIZED: Hot path implementation - target < 50ns latency
    void MessageRouter::routeMessage(FixMessage *message) noexcept
    {
        // FAST PATH: Null check
        if (!message)
        {
            stats_.routing_errors.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // OPTIMIZED: High-resolution timing for sub-nanosecond measurement
        auto start_time = std::chrono::high_resolution_clock::now();

        // OPTIMIZED: Direct priority mapping with inlined method call
        Priority priority = getMessagePriority(message);
        
        // OPTIMIZED: Zero-copy pointer move to appropriate queue
        if (tryRouteToQueue(message, priority))
        {
            // SUCCESS: Record performance metrics
            auto end_time = std::chrono::high_resolution_clock::now();
            auto routing_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
            recordRoutingSuccess(priority, routing_time_ns);
        }
        else
        {
            // FAILURE: Queue full - record drop (no logging in hot path)
            recordRoutingFailure(priority);
        }
    }

    void MessageRouter::routeMessages(FixMessage **messages, size_t count) noexcept
    {
        if (!messages || count == 0)
        {
            return;
        }

        // OPTIMIZED: Batch processing with prefetch hints
        for (size_t i = 0; i < count; ++i)
        {
            // Prefetch next message for better cache performance
            if (i + 1 < count)
            {
                PREFETCH(messages[i + 1]);
            }
            
            routeMessage(messages[i]);
        }
    }

    // OPTIMIZED: Inlined priority mapping with branch prediction optimization
    Priority MessageRouter::getMessagePriority(const FixMessage *message) const noexcept
    {
        // ULTRA-FAST DIRECT MAPPING: FixMsgType → Priority
        FixMsgType msgType = message->getMsgTypeEnum();

        // OPTIMIZED: Most common messages first (execution reports, heartbeats)
        if (msgType == FixMsgType::EXECUTION_REPORT)
        {
            return Priority::CRITICAL;
        }
        
        if (msgType == FixMsgType::HEARTBEAT)
        {
            return Priority::LOW;
        }

        // Less common but still high-frequency messages
        switch (msgType)
        {
            // CRITICAL: Trading messages (most latency-sensitive)
            case FixMsgType::ORDER_CANCEL_REJECT:
            case FixMsgType::NEW_ORDER_SINGLE:
            case FixMsgType::ORDER_CANCEL_REQUEST:
            case FixMsgType::ORDER_CANCEL_REPLACE_REQUEST:
            case FixMsgType::ORDER_STATUS_REQUEST:
                return Priority::CRITICAL;

            // HIGH: Market data messages  
            case FixMsgType::MARKET_DATA_REQUEST:
            case FixMsgType::MARKET_DATA_SNAPSHOT:
            case FixMsgType::MARKET_DATA_INCREMENTAL_REFRESH:
            case FixMsgType::MARKET_DATA_REQUEST_REJECT:
                return Priority::HIGH;

            // MEDIUM: Session administrative messages
            case FixMsgType::TEST_REQUEST:
            case FixMsgType::RESEND_REQUEST:
            case FixMsgType::REJECT:
            case FixMsgType::SEQUENCE_RESET:
            case FixMsgType::LOGOUT:
            case FixMsgType::LOGON:
                return Priority::MEDIUM;

            // DEFAULT: Unknown message types go to low priority
            case FixMsgType::UNKNOWN:
            default:
                return Priority::LOW;
        }
    }

    // OPTIMIZED: Compile-time constant priority index lookup
    constexpr int MessageRouter::getPriorityIndex(Priority priority) const noexcept
    {
        return static_cast<int>(priority);
    }

    // OPTIMIZED: Zero-copy pointer move with branch prediction
    bool MessageRouter::tryRouteToQueue(FixMessage *message, Priority priority) noexcept
    {
        int queue_index = getPriorityIndex(priority);
        
        // OPTIMIZED: Direct access to queue array (no bounds checking in release)
        auto &target_queue = queues_->getQueues()[queue_index];
        
        // ZERO-COPY: Direct pointer move to queue (no copying)
        return target_queue->push(message);
    }

    // OPTIMIZED: Lock-free performance tracking (no mutex overhead)
    inline void MessageRouter::recordRoutingSuccess(Priority priority, uint64_t routing_time_ns) noexcept
    {
        // Update global counters
        stats_.messages_routed.fetch_add(1, std::memory_order_relaxed);
        stats_.total_routing_time_ns.fetch_add(routing_time_ns, std::memory_order_relaxed);
        
        // Update peak latency (lock-free compare-and-swap)
        uint64_t current_peak = stats_.peak_routing_time_ns.load(std::memory_order_relaxed);
        while (routing_time_ns > current_peak && 
               !stats_.peak_routing_time_ns.compare_exchange_weak(
                   current_peak, routing_time_ns, std::memory_order_relaxed))
        {
            // Retry if another thread updated peak
        }
        
        // Update per-priority counters
        switch (priority)
        {
            case Priority::CRITICAL:
                stats_.critical_routed.fetch_add(1, std::memory_order_relaxed);
                break;
            case Priority::HIGH:
                stats_.high_routed.fetch_add(1, std::memory_order_relaxed);
                break;
            case Priority::MEDIUM:
                stats_.medium_routed.fetch_add(1, std::memory_order_relaxed);
                break;
            case Priority::LOW:
                stats_.low_routed.fetch_add(1, std::memory_order_relaxed);
                break;
        }
    }

    // OPTIMIZED: Lock-free error tracking (no logging overhead)
    inline void MessageRouter::recordRoutingFailure(Priority priority) noexcept
    {
        stats_.messages_dropped.fetch_add(1, std::memory_order_relaxed);
        
        // Update per-priority drop counters
        switch (priority)
        {
            case Priority::CRITICAL:
                stats_.critical_dropped.fetch_add(1, std::memory_order_relaxed);
                break;
            case Priority::HIGH:
                stats_.high_dropped.fetch_add(1, std::memory_order_relaxed);
                break;
            case Priority::MEDIUM:
                stats_.medium_dropped.fetch_add(1, std::memory_order_relaxed);
                break;
            case Priority::LOW:
                stats_.low_dropped.fetch_add(1, std::memory_order_relaxed);
                break;
        }
    }

} // namespace fix_gateway::manager