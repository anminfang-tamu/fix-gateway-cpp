#include "manager/message_router.h"
#include "utils/logger.h"
#include "protocol/fix_fields.h"

namespace fix_gateway::manager
{
    using namespace fix_gateway::protocol;

    MessageRouter::MessageRouter()
        : running_(false)
    {
        // Initialize 4 separate priority queues using proper array indices (0-3)
        priority_queues_[getPriorityIndex(Priority::CRITICAL)] =
            std::make_unique<FixMessageQueue>(2048, "critical_queue");
        priority_queues_[getPriorityIndex(Priority::HIGH)] =
            std::make_unique<FixMessageQueue>(2048, "high_queue");
        priority_queues_[getPriorityIndex(Priority::MEDIUM)] =
            std::make_unique<FixMessageQueue>(1024, "medium_queue");
        priority_queues_[getPriorityIndex(Priority::LOW)] =
            std::make_unique<FixMessageQueue>(512, "low_queue");
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

        // Start single worker thread that processes all priorities
        worker_threads_.emplace_back(&MessageRouter::processAllQueues, this);

        LOG_INFO("MessageRouter started with " + std::to_string(worker_threads_.size()) + " worker threads");
    }

    void MessageRouter::stop()
    {
        running_.store(false, std::memory_order_relaxed);

        // Wait for all worker threads to finish
        for (auto &thread : worker_threads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        worker_threads_.clear();

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
        // Convert Priority enum (1-4) to array index (0-3)
        return static_cast<int>(priority) - 1;
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
        if (!priority_queues_[queue_index]->push(message))
        {
            LOG_ERROR("Failed to enqueue message to priority queue " + std::to_string(queue_index));
        }
    }

    void MessageRouter::processAllQueues()
    {
        LOG_INFO("Message router worker thread started");

        while (running_.load(std::memory_order_relaxed))
        {
            bool processed_any = false;

            // Process queues in priority order (CRITICAL first)
            // Priority enum values are 1-4, so we iterate through array indices 0-3
            for (int priority_val = static_cast<int>(Priority::CRITICAL);
                 priority_val <= static_cast<int>(Priority::LOW);
                 ++priority_val)
            {
                int queue_index = getPriorityIndex(static_cast<Priority>(priority_val));
                FixMessage *fix_message = nullptr;

                if (priority_queues_[queue_index]->tryPop(fix_message))
                {
                    // Directly process FixMessage* (no mapping needed!)
                    if (fix_message)
                    {
                        // Determine if it's session or business message using direct priority mapping
                        Priority msg_priority = getMessagePriority(fix_message);
                        if (msg_priority == Priority::LOW || msg_priority == Priority::MEDIUM)
                        {
                            // Session messages: HEARTBEAT (LOW), SESSION_ADMIN (MEDIUM)
                            processSessionMessage(fix_message);
                        }
                        else
                        {
                            // Business messages: MARKET_DATA (HIGH), TRADING (CRITICAL)
                            processBusinessMessage(fix_message);
                        }
                    }
                    processed_any = true;
                }
            }

            // If no messages processed, sleep briefly to avoid busy waiting
            if (!processed_any)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        LOG_INFO("Message router worker thread stopped");
    }

    void MessageRouter::processSessionMessage(FixMessage *message)
    {
        if (session_handler_)
        {
            try
            {
                session_handler_(message);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Exception in session handler: " + std::string(e.what()));
            }
        }
        else
        {
            LOG_WARN("No session handler registered for message: " + message->getMsgType());
        }
    }

    void MessageRouter::processBusinessMessage(FixMessage *message)
    {
        if (business_handler_)
        {
            try
            {
                business_handler_(message);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Exception in business handler: " + std::string(e.what()));
            }
        }
        else
        {
            LOG_WARN("No business handler registered for message: " + message->getMsgType());
        }
    }

    void MessageRouter::registerSessionHandler(SessionHandler handler)
    {
        session_handler_ = handler;
        LOG_INFO("Session handler registered");
    }

    void MessageRouter::registerBusinessHandler(BusinessHandler handler)
    {
        business_handler_ = handler;
        LOG_INFO("Business handler registered");
    }

    size_t MessageRouter::getQueueDepth(Priority priority) const
    {
        int index = static_cast<int>(priority) - 1;
        if (index >= 0 && index < NUM_PRIORITY_QUEUES)
        {
            return priority_queues_[index]->size();
        }
        return 0;
    }

} // namespace fix_gateway::manager