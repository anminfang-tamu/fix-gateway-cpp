#pragma once

#include "protocol/fix_message.h"
#include "utils/lockfree_queue.h"
#include "../../config/priority_config.h"

#include <memory>
#include <array>

class PriorityQueueContainer
{
public:
    using FixMessage = fix_gateway::protocol::FixMessage;
    using LockFreeQueue = fix_gateway::utils::LockFreeQueue<FixMessage *>;
    using FixMessageQueuePtr = std::shared_ptr<LockFreeQueue>;
    using QueueArray = std::array<FixMessageQueuePtr, 4>;

    PriorityQueueContainer()
    {
        queues_[getPriorityIndex(Priority::CRITICAL)] = std::make_shared<LockFreeQueue>(2048, "critical_queue");
        queues_[getPriorityIndex(Priority::HIGH)] = std::make_shared<LockFreeQueue>(2048, "high_queue");
        queues_[getPriorityIndex(Priority::MEDIUM)] = std::make_shared<LockFreeQueue>(1024, "medium_queue");
        queues_[getPriorityIndex(Priority::LOW)] = std::make_shared<LockFreeQueue>(512, "low_queue");
    }

    int getPriorityIndex(Priority priority)
    {
        return static_cast<int>(priority);
    }

    FixMessageQueuePtr getQueue(Priority priority)
    {
        return queues_[getPriorityIndex(priority)];
    }

    const QueueArray &getQueues() const
    {
        return queues_;
    }

private:
    QueueArray queues_;
};