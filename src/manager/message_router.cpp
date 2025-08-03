#include "manager/message_router.h"
#include "utils/logger.h"
#include "protocol/fix_fields.h"

namespace fix_gateway::manager
{
    using namespace fix_gateway::protocol;

    MessageRouter::MessageRouter(std::shared_ptr<PriorityQueueContainer> queues)
        : running_(false), queues_(queues)
    {
    }

    MessageRouter::~MessageRouter()
    {
        stop();
    }

    void MessageRouter::start()
    {
        if (running_.load())
        {
            return; // Already running
        }

        running_.store(true, std::memory_order_relaxed);

        LOG_INFO("MessageRouter started - ready to route messages");
    }

    void MessageRouter::stop()
    {
        running_.store(false, std::memory_order_relaxed);

        LOG_INFO("MessageRouter stopped");
    }

    Priority MessageRouter::getMessagePriority(const FixMessage *message)
    {
        // ULTRA-FAST DIRECT MAPPING: FixMsgType → Priority (no intermediate enum!)
        FixMsgType msgType = message->getMsgTypeEnum();

        switch (msgType)
        {
        // CRITICAL: Trading messages (execution reports, order responses)
        case FixMsgType::EXECUTION_REPORT:
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

        // LOW: Heartbeat messages (lowest latency requirement)
        case FixMsgType::HEARTBEAT:
            return Priority::LOW;

        // DEFAULT: Unknown message types
        case FixMsgType::UNKNOWN:
        default:
            return Priority::LOW;
        }
    }

    int MessageRouter::getPriorityIndex(Priority priority)
    {
        return static_cast<int>(priority);
    }

    void MessageRouter::routeMessage(FixMessage *message)
    {
        // 1. Validate message
        if (!message)
        {
            LOG_ERROR("Null message passed to routeMessage");
            return;
        }

        // 2. Direct priority mapping and enqueue
        Priority system_priority = getMessagePriority(message);
        int queue_index = getPriorityIndex(system_priority);

        // 3. Directly enqueue FixMessage* to the appropriate priority queue
        // TODO: if queue is full, drop message, this need to put message to a drop queue and try again later
        if (!queues_->getQueues()[queue_index]->push(message))
        {
            LOG_ERROR("Failed to enqueue message to priority queue " + std::to_string(queue_index));
        }
    }
} // namespace fix_gateway::manager