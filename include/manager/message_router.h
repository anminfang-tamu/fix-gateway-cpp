#pragma once

#include "utils/lockfree_priority_queue.h"
#include "protocol/fix_message.h"
#include "../../config/priority_config.h"

#include <thread>
#include <atomic>
#include <functional>
#include <array>
#include <memory>
#include <vector>

namespace fix_gateway::manager
{
    using FixMessage = fix_gateway::protocol::FixMessage;
    using FixMessageQueue = fix_gateway::utils::LockFreeQueue<FixMessage *>;

    class MessageRouter
    {
    public:
        using SessionHandler = std::function<void(FixMessage *)>;
        using BusinessHandler = std::function<void(FixMessage *)>;

        MessageRouter();
        ~MessageRouter();

        // lifecycle
        void start();
        void stop();

        // core routing entry point
        void routeMessage(FixMessage *message);

        // register handlers
        void registerSessionHandler(SessionHandler handler);
        void registerBusinessHandler(BusinessHandler handler);

        // monitoring
        bool isRunning() const { return running_.load(); }
        size_t getQueueDepth(Priority priority) const;

    private:
        // Direct message to priority mapping (optimized - no intermediate enum)
        Priority getMessagePriority(const FixMessage *message);
        int getPriorityIndex(Priority priority);

        // worker thread functions
        void processMessageQueue(Priority priority);
        void processAllQueues(); // Single worker processes all priorities

        // message processing
        void processSessionMessage(FixMessage *message);
        void processBusinessMessage(FixMessage *message);

        // infrastructure - 4 separate queues for each priority level
        static constexpr size_t NUM_PRIORITY_QUEUES = 4;
        std::array<std::unique_ptr<FixMessageQueue>, NUM_PRIORITY_QUEUES> priority_queues_;
        std::vector<std::thread> worker_threads_;
        std::atomic<bool> running_;

        // handlers
        SessionHandler session_handler_;
        BusinessHandler business_handler_;
    };

} // namespace fix_gateway::manager