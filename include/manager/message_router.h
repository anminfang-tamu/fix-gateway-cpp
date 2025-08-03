#pragma once

#include "utils/lockfree_queue.h"
#include "protocol/fix_message.h"
#include "../application/priority_queue_container.h"
#include "../../config/priority_config.h"

#include <atomic>
#include <memory>

namespace fix_gateway::manager
{
    using FixMessage = fix_gateway::protocol::FixMessage;
    using FixMessageQueue = fix_gateway::utils::LockFreeQueue<FixMessage *>;

    class MessageRouter
    {
    public:
        MessageRouter(std::shared_ptr<PriorityQueueContainer> queues);
        ~MessageRouter();

        // lifecycle
        void start();
        void stop();

        // core routing entry point
        void routeMessage(FixMessage *message);

        // monitoring
        bool isRunning() const { return running_.load(); }

    private:
        // Direct message to priority mapping (optimized - no intermediate enum)
        Priority getMessagePriority(const FixMessage *message);
        int getPriorityIndex(Priority priority);

        // infrastructure - shared priority queues
        std::shared_ptr<PriorityQueueContainer> queues_;
        std::atomic<bool> running_;
    };

} // namespace fix_gateway::manager