#include "manager/inbound_message_manager.h"
#include "protocol/fix_fields.h"
#include "utils/logger.h"

#include <chrono>
#include <thread>

using namespace fix_gateway::manager;
using namespace fix_gateway::protocol;
using namespace fix_gateway::utils;

// =================================================================
// CONSTRUCTOR AND DESTRUCTOR
// =================================================================

InboundMessageManager::InboundMessageManager(const std::string &manager_name)
    : manager_name_(manager_name), running_(false)
{
    logInfo("Created InboundMessageManager: " + manager_name_);
}

// =================================================================
// LIFECYCLE MANAGEMENT
// =================================================================

void InboundMessageManager::start()
{
    if (running_.load())
    {
        logWarning("InboundMessageManager already running");
        return;
    }

    if (!inbound_queue_ || !outbound_queues_)
    {
        logError("Cannot start - queues not configured");
        return;
    }

    running_.store(true);
    logInfo("InboundMessageManager started: " + manager_name_);
}

void InboundMessageManager::stop()
{
    if (!running_.load())
    {
        return;
    }

    running_.store(false);
    logInfo("InboundMessageManager stopped: " + manager_name_);
}

// =================================================================
// QUEUE INTEGRATION - CORE ARCHITECTURE
// =================================================================

void InboundMessageManager::setInboundQueue(std::shared_ptr<LockFreeQueue> inbound_queue)
{
    inbound_queue_ = inbound_queue;
    logInfo("Inbound queue connected to " + manager_name_);
}

void InboundMessageManager::setOutboundQueues(std::shared_ptr<PriorityQueueContainer> outbound_queues)
{
    outbound_queues_ = outbound_queues;
    logInfo("Outbound queues connected to " + manager_name_);
}

// =================================================================
// MESSAGE PROCESSING LOOP
// =================================================================

void InboundMessageManager::processMessages()
{
    if (!running_.load() || !inbound_queue_)
    {
        logError("Cannot process - manager not running or queue not set");
        return;
    }

    logDebug("Starting message processing loop for " + manager_name_);

    while (running_.load())
    {
        FixMessage *message = nullptr;

        // Poll inbound queue for messages
        if (inbound_queue_->tryPop(message))
        {
            if (message)
            {
                bool processed = processMessage(message);
                if (!processed)
                {
                    logWarning("Failed to process message");
                }
            }
        }
        else
        {
            // No messages available - short sleep to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }

    logDebug("Message processing loop ended for " + manager_name_);
}

bool InboundMessageManager::processMessage(FixMessage *message)
{
    if (!message)
    {
        logError("Null message passed to processMessage");
        return false;
    }

    // Update last message time
    stats_.last_message_time = std::chrono::steady_clock::now();

    // Record processing start
    recordProcessingStart();

    // Extract message type for logging and stats
    std::string msg_type_str;
    FixMsgType msg_type = FixMsgType::UNKNOWN;

    if (message->getField(FixFields::MsgType, msg_type_str))
    {
        // Convert string to enum - simplified mapping
        if (msg_type_str == "A")
            msg_type = FixMsgType::LOGON;
        else if (msg_type_str == "5")
            msg_type = FixMsgType::LOGOUT;
        else if (msg_type_str == "0")
            msg_type = FixMsgType::HEARTBEAT;
        else if (msg_type_str == "1")
            msg_type = FixMsgType::TEST_REQUEST;
        else if (msg_type_str == "D")
            msg_type = FixMsgType::NEW_ORDER_SINGLE;
        else if (msg_type_str == "F")
            msg_type = FixMsgType::ORDER_CANCEL_REQUEST;
        // Add more mappings as needed
    }

    bool success = false;
    bool routed = false;

    try
    {
        // Validate message
        if (!validateMessage(message))
        {
            logError("Message validation failed");
            recordProcessingEnd(msg_type, false, false);
            return false;
        }

        // Check if this manager can handle the message
        if (!canHandleMessage(message))
        {
            logWarning("Message type not supported by " + manager_name_ + ": " + msg_type_str);
            recordProcessingEnd(msg_type, false, false);
            return false;
        }

        // Handle the message (implemented by child classes)
        success = handleMessage(message);

        if (success)
        {
            stats_.total_messages_processed++;
            routed = true; // Assume child class routed the response
        }
        else
        {
            stats_.total_processing_errors++;
            logError("Message handling failed for message type: " + msg_type_str);
        }
    }
    catch (const std::exception &e)
    {
        logError("Exception processing message: " + std::string(e.what()));
        stats_.total_processing_errors++;
        success = false;
    }

    // Record processing metrics
    recordProcessingEnd(msg_type, success, routed);

    return success;
}

// =================================================================
// PUBLIC INTERFACE FOR TESTING AND EXTERNAL ACCESS
// =================================================================

bool InboundMessageManager::canHandleMessage(const FixMessage *message) const
{
    return isMessageSupported(message);
}

std::vector<FixMsgType> InboundMessageManager::getSupportedMessageTypes() const
{
    return getHandledMessageTypes();
}

// =================================================================
// MESSAGE ROUTING TO OUTBOUND QUEUES
// =================================================================

bool InboundMessageManager::routeToOutbound(FixMessage *message, Priority priority)
{
    if (!message || !outbound_queues_)
    {
        logError("Cannot route message - null message or queues not set");
        return false;
    }

    auto target_queue = outbound_queues_->getQueue(priority);
    if (!target_queue)
    {
        logError("Failed to get outbound queue for priority: " + std::to_string(static_cast<int>(priority)));
        return false;
    }

    bool enqueued = target_queue->push(message);

    if (enqueued)
    {
        stats_.total_messages_routed++;

        // Update priority-specific stats
        switch (priority)
        {
        case Priority::CRITICAL:
            stats_.critical_routed++;
            break;
        case Priority::HIGH:
            stats_.high_routed++;
            break;
        case Priority::MEDIUM:
            stats_.medium_routed++;
            break;
        case Priority::LOW:
            stats_.low_routed++;
            break;
        }

        logDebug("Message routed to " + std::to_string(static_cast<int>(priority)) + " priority queue");
    }
    else
    {
        stats_.total_routing_errors++;
        logError("Failed to enqueue message to outbound queue");
    }

    return enqueued;
}

bool InboundMessageManager::sendResponse(FixMessage *response_message, Priority priority)
{
    if (!response_message)
    {
        logError("Cannot send null response message");
        return false;
    }

    bool routed = routeToOutbound(response_message, priority);
    if (routed)
    {
        logDebug("Response message routed successfully");
    }
    else
    {
        logError("Failed to route response message");
        // Note: Don't delete here - message pool manages memory
    }

    return routed;
}

// =================================================================
// MESSAGE VALIDATION HELPERS
// =================================================================

bool InboundMessageManager::validateMessage(const FixMessage *message) const
{
    if (!message)
    {
        return false;
    }

    // Check for required fields
    std::string msg_type, sender_comp_id, target_comp_id, seq_num;

    if (!message->getField(FixFields::MsgType, msg_type) ||
        !message->getField(FixFields::SenderCompID, sender_comp_id) ||
        !message->getField(FixFields::TargetCompID, target_comp_id) ||
        !message->getField(FixFields::MsgSeqNum, seq_num))
    {
        return false;
    }

    // Basic field validation
    if (msg_type.empty() || sender_comp_id.empty() ||
        target_comp_id.empty() || seq_num.empty())
    {
        return false;
    }

    try
    {
        // Validate sequence number is numeric
        int seq = std::stoi(seq_num);
        if (seq <= 0)
        {
            return false;
        }
    }
    catch (const std::exception &)
    {
        return false;
    }

    return true;
}

bool InboundMessageManager::isSessionMessage(FixMsgType msg_type) const
{
    return isSessionMessageType(msg_type);
}

bool InboundMessageManager::isTradingMessage(FixMsgType msg_type) const
{
    return isTradingMessageType(msg_type);
}

// =================================================================
// PRIORITY DETERMINATION BASED ON MESSAGE TYPE
// =================================================================

Priority InboundMessageManager::getMessagePriority(FixMsgType msg_type) const
{
    switch (msg_type)
    {
    // Session-critical messages
    case FixMsgType::LOGON:
    case FixMsgType::LOGOUT:
        return Priority::CRITICAL;

    // Time-sensitive session messages
    case FixMsgType::HEARTBEAT:
    case FixMsgType::TEST_REQUEST:
        return Priority::HIGH;

    // Trading execution messages
    case FixMsgType::EXECUTION_REPORT:
    case FixMsgType::ORDER_CANCEL_REJECT:
        return Priority::HIGH;

    // Standard trading flow
    case FixMsgType::NEW_ORDER_SINGLE:
    case FixMsgType::ORDER_CANCEL_REQUEST:
    case FixMsgType::ORDER_CANCEL_REPLACE_REQUEST:
        return Priority::MEDIUM;

    // Administrative messages
    case FixMsgType::SEQUENCE_RESET:
    case FixMsgType::RESEND_REQUEST:
    case FixMsgType::REJECT:
        return Priority::MEDIUM;

    // Low priority messages
    case FixMsgType::ORDER_STATUS_REQUEST:
    default:
        return Priority::LOW;
    }
}

// =================================================================
// PERFORMANCE MONITORING HELPERS
// =================================================================

void InboundMessageManager::recordProcessingStart()
{
    processing_start_time_ = std::chrono::steady_clock::now();
}

void InboundMessageManager::recordProcessingEnd(FixMsgType msg_type, bool success, bool routed)
{
    auto end_time = std::chrono::steady_clock::now();
    auto processing_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - processing_start_time_);

    updateStats(msg_type, processing_time, success, routed);
}

void InboundMessageManager::updateStats(FixMsgType msg_type, std::chrono::nanoseconds processing_time,
                                        bool success, bool routed)
{
    // Update overall processing time average
    double new_time_ns = static_cast<double>(processing_time.count());

    if (stats_.total_messages_processed == 0)
    {
        stats_.avg_processing_time_ns = new_time_ns;
    }
    else
    {
        // Running average
        stats_.avg_processing_time_ns =
            (stats_.avg_processing_time_ns * stats_.total_messages_processed + new_time_ns) /
            (stats_.total_messages_processed + 1);
    }

    // Update per-message-type stats
    stats_.message_type_counts[msg_type]++;

    if (stats_.message_type_avg_latency.find(msg_type) == stats_.message_type_avg_latency.end())
    {
        stats_.message_type_avg_latency[msg_type] = new_time_ns;
    }
    else
    {
        uint64_t count = stats_.message_type_counts[msg_type];
        stats_.message_type_avg_latency[msg_type] =
            (stats_.message_type_avg_latency[msg_type] * (count - 1) + new_time_ns) / count;
    }
}

// =================================================================
// LOGGING HELPERS
// =================================================================

void InboundMessageManager::logInfo(const std::string &message) const
{
    Logger::getInstance().info("[" + manager_name_ + "] " + message);
}

void InboundMessageManager::logError(const std::string &message) const
{
    Logger::getInstance().error("[" + manager_name_ + "] " + message);
}

void InboundMessageManager::logWarning(const std::string &message) const
{
    Logger::getInstance().warn("[" + manager_name_ + "] " + message);
}

void InboundMessageManager::logDebug(const std::string &message) const
{
    Logger::getInstance().debug("[" + manager_name_ + "] " + message);
}

// =================================================================
// MESSAGE TYPE CLASSIFICATION
// =================================================================

bool InboundMessageManager::isSessionMessageType(FixMsgType msg_type) const
{
    switch (msg_type)
    {
    case FixMsgType::LOGON:
    case FixMsgType::LOGOUT:
    case FixMsgType::HEARTBEAT:
    case FixMsgType::TEST_REQUEST:
    case FixMsgType::RESEND_REQUEST:
    case FixMsgType::SEQUENCE_RESET:
    case FixMsgType::REJECT:
        return true;
    default:
        return false;
    }
}

bool InboundMessageManager::isTradingMessageType(FixMsgType msg_type) const
{
    switch (msg_type)
    {
    case FixMsgType::NEW_ORDER_SINGLE:
    case FixMsgType::ORDER_CANCEL_REQUEST:
    case FixMsgType::ORDER_CANCEL_REPLACE_REQUEST:
    case FixMsgType::EXECUTION_REPORT:
    case FixMsgType::ORDER_CANCEL_REJECT:
    case FixMsgType::ORDER_STATUS_REQUEST:
        return true;
    default:
        return false;
    }
}

bool InboundMessageManager::isAdminMessageType(FixMsgType msg_type) const
{
    switch (msg_type)
    {
    case FixMsgType::MARKET_DATA_REQUEST:
    case FixMsgType::MARKET_DATA_SNAPSHOT:
    case FixMsgType::MARKET_DATA_INCREMENTAL_REFRESH:
    case FixMsgType::MARKET_DATA_REQUEST_REJECT:
        return true;
    default:
        return false;
    }
}

// =================================================================
// MESSAGE MANAGER FACTORY
// =================================================================

std::unique_ptr<InboundMessageManager> MessageManagerFactory::createFixSessionManager(
    const std::string &manager_name)
{
    // TODO: Implement factory method
    // For now, return nullptr - this needs proper FixSessionManager instantiation
    return nullptr;
}

std::unique_ptr<InboundMessageManager> MessageManagerFactory::createBusinessLogicManager(
    const std::string &manager_name)
{
    // TODO: Implement factory method
    // For now, return nullptr - this needs proper BusinessLogicManager instantiation
    return nullptr;
}