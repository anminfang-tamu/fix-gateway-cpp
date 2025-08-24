#include "manager/fix_session_manager.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"
#include "utils/logger.h"

#include <sstream>
#include <iomanip>
#include <thread>

using namespace fix_gateway::manager;
using namespace fix_gateway::protocol;
using namespace fix_gateway::utils;
using namespace fix_gateway::common;

// =================================================================
// CONSTRUCTOR AND DESTRUCTOR
// =================================================================

FixSessionManager::FixSessionManager(const SessionConfig &config)
    : InboundMessageManager("FixSessionManager"), config_(config)
{
    logInfo("Created FixSessionManager for " + config_.sender_comp_id + " -> " + config_.target_comp_id);
    session_stats_.current_state = SessionState::DISCONNECTED;
}

FixSessionManager::~FixSessionManager()
{
    stop();
}

// =================================================================
// LIFECYCLE MANAGEMENT
// =================================================================

void FixSessionManager::start()
{
    InboundMessageManager::start();

    session_stats_.session_start_time = std::chrono::steady_clock::now();
    logInfo("FixSessionManager started");
}

void FixSessionManager::stop()
{
    stopHeartbeatTimer();
    updateSessionState(SessionState::DISCONNECTED);

    InboundMessageManager::stop();
    logInfo("FixSessionManager stopped");
}

// =================================================================
// SESSION MANAGEMENT
// =================================================================

bool FixSessionManager::initiateLogon()
{
    if (session_state_.load() != SessionState::DISCONNECTED)
    {
        logWarning("Cannot initiate logon - session not in DISCONNECTED state");
        return false;
    }

    updateSessionState(SessionState::LOGON_SENT);

    bool success = sendLogon();
    if (success)
    {
        session_stats_.logons_sent++;
        logInfo("Logon initiated successfully");
    }
    else
    {
        updateSessionState(SessionState::DISCONNECTED);
        logError("Failed to initiate logon");
    }

    return success;
}

bool FixSessionManager::initiateLogout(const std::string &reason)
{
    SessionState current_state = session_state_.load();
    if (current_state != SessionState::LOGGED_ON && current_state != SessionState::LOGON_SENT)
    {
        logWarning("Cannot initiate logout - session not logged on");
        return false;
    }

    updateSessionState(SessionState::LOGOUT_SENT);

    bool success = sendLogout(reason);
    if (success)
    {
        session_stats_.logouts_sent++;
        logInfo("Logout initiated successfully" + (reason.empty() ? "" : " - " + reason));
    }
    else
    {
        logError("Failed to initiate logout");
    }

    return success;
}

void FixSessionManager::updateHeartbeatInterval(int seconds)
{
    config_.heartbeat_interval = seconds;
    logInfo("Updated heartbeat interval to " + std::to_string(seconds) + " seconds");
}

void FixSessionManager::setSequenceNumbers(int incoming_seq, int outgoing_seq)
{
    expected_incoming_seq_num_.store(incoming_seq);
    outgoing_seq_num_.store(outgoing_seq);
    logInfo("Set sequence numbers - incoming: " + std::to_string(incoming_seq) +
            ", outgoing: " + std::to_string(outgoing_seq));
}

void FixSessionManager::setMessagePool(std::shared_ptr<fix_gateway::common::MessagePool<FixMessage>> message_pool)
{
    message_pool_ = message_pool;
    logInfo("Message pool connected to FixSessionManager");
}

// =================================================================
// ABSTRACT METHOD IMPLEMENTATIONS
// =================================================================

bool FixSessionManager::handleMessage(FixMessage *message)
{
    if (!message)
    {
        return false;
    }

    // Update last message received time
    last_message_received_ = std::chrono::steady_clock::now();

    // Extract message type
    std::string msg_type_str;
    if (!message->getField(FixFields::MsgType, msg_type_str))
    {
        logError("Message missing MsgType field");
        return false;
    }

    // Validate session-level fields
    if (!validateSessionMessage(message))
    {
        logError("Session message validation failed");
        return false;
    }

    // Validate sequence number
    if (config_.validate_sequence_numbers && !validateSequenceNumber(message))
    {
        logError("Sequence number validation failed");
        return false;
    }

    // Route to appropriate handler
    bool handled = false;
    try
    {
        if (msg_type_str == "A")
            handled = handleLogon(message);
        else if (msg_type_str == "5")
            handled = handleLogout(message);
        else if (msg_type_str == "0")
            handled = handleHeartbeat(message);
        else if (msg_type_str == "1")
            handled = handleTestRequest(message);
        else if (msg_type_str == "2")
            handled = handleResendRequest(message);
        else if (msg_type_str == "4")
            handled = handleSequenceReset(message);
        else if (msg_type_str == "3")
            handled = handleReject(message);
        else
        {
            logWarning("Unsupported session message type: " + msg_type_str);
            return false;
        }
    }
    catch (const std::exception &e)
    {
        logError("Exception handling session message: " + std::string(e.what()));
        return false;
    }

    return handled;
}

bool FixSessionManager::isMessageSupported(const FixMessage *message) const
{
    if (!message)
    {
        return false;
    }

    std::string msg_type;
    if (!message->getField(FixFields::MsgType, msg_type))
    {
        return false;
    }

    // Check if it's a session-level message
    return (msg_type == "A" || msg_type == "5" || msg_type == "0" ||
            msg_type == "1" || msg_type == "2" || msg_type == "4" || msg_type == "3");
}

std::vector<FixMsgType> FixSessionManager::getHandledMessageTypes() const
{
    return {
        FixMsgType::LOGON,
        FixMsgType::LOGOUT,
        FixMsgType::HEARTBEAT,
        FixMsgType::TEST_REQUEST,
        FixMsgType::RESEND_REQUEST,
        FixMsgType::SEQUENCE_RESET,
        FixMsgType::REJECT};
}

// =================================================================
// SESSION MESSAGE HANDLERS
// =================================================================

bool FixSessionManager::handleLogon(FixMessage *message)
{
    logInfo("Processing Logon message");

    // Extract heartbeat interval
    std::string heartbeat_str;
    if (message->getField(FixFields::HeartBtInt, heartbeat_str))
    {
        try
        {
            int heartbeat_interval = std::stoi(heartbeat_str);
            if (heartbeat_interval > 0)
            {
                config_.heartbeat_interval = heartbeat_interval;
                logInfo("Updated heartbeat interval from logon: " + std::to_string(heartbeat_interval));
            }
        }
        catch (const std::exception &e)
        {
            logWarning("Invalid heartbeat interval in logon: " + heartbeat_str);
        }
    }

    // Update session state
    updateSessionState(SessionState::LOGGED_ON);

    // Start heartbeat timer
    startHeartbeatTimer();

    return true;
}

bool FixSessionManager::handleLogout(FixMessage *message)
{
    logInfo("Processing Logout message");

    // Extract logout reason if present
    std::string logout_text;
    message->getField(FixFields::Text, logout_text);

    if (!logout_text.empty())
    {
        logInfo("Logout reason: " + logout_text);
    }

    // Stop heartbeat timer
    stopHeartbeatTimer();

    // Send logout response if needed
    SessionState current_state = session_state_.load();
    if (current_state == SessionState::LOGGED_ON)
    {
        bool response_sent = sendLogout("Logout acknowledged");
        if (response_sent)
        {
            logInfo("Logout response sent");
        }
    }

    // Update session state
    updateSessionState(SessionState::DISCONNECTED);

    return true;
}

bool FixSessionManager::handleHeartbeat(FixMessage *message)
{
    logDebug("Processing Heartbeat message");

    session_stats_.heartbeats_received++;
    session_stats_.last_heartbeat_time = std::chrono::steady_clock::now();

    // Check if this is a response to our test request
    std::string test_req_id;
    if (message->getField(FixFields::TestReqID, test_req_id))
    {
        if (test_req_id == pending_test_request_id_)
        {
            logDebug("Received heartbeat response to test request: " + test_req_id);
            pending_test_request_id_.clear();
        }
    }

    return true;
}

bool FixSessionManager::handleTestRequest(FixMessage *message)
{
    logDebug("Processing TestRequest message");

    session_stats_.test_requests_received++;

    // Extract test request ID
    std::string test_req_id;
    if (!message->getField(FixFields::TestReqID, test_req_id))
    {
        logError("TestRequest missing TestReqID field");
        return false;
    }

    // Send heartbeat response with same TestReqID
    bool response_sent = sendHeartbeat(test_req_id);
    if (!response_sent)
    {
        logError("Failed to send heartbeat response to test request");
        return false;
    }

    logDebug("Sent heartbeat response to test request: " + test_req_id);
    return true;
}

bool FixSessionManager::handleResendRequest(FixMessage *message)
{
    logInfo("Processing ResendRequest from broker");

    std::string begin_seq_str, end_seq_str;
    if (!message->getField(FixFields::BeginSeqNo, begin_seq_str) ||
        !message->getField(FixFields::EndSeqNo, end_seq_str))
    {
        logError("ResendRequest missing sequence number fields");
        return false;
    }

    try
    {
        int begin_seq = std::stoi(begin_seq_str);
        int end_seq = std::stoi(end_seq_str);

        logInfo("Broker requests resend for messages " + std::to_string(begin_seq) +
                " to " + std::to_string(end_seq));

        // Performance-focused client: gap fill instead of storing/resending messages
        bool gap_fill_sent = sendSequenceReset(end_seq + 1, true);
        if (!gap_fill_sent)
        {
            logError("Failed to send gap fill sequence reset");
            return false;
        }

        logInfo("Gap filled - continuing transaction flow");
        return true;
    }
    catch (const std::exception &e)
    {
        logError("Invalid sequence numbers in resend request: " + std::string(e.what()));
        return false;
    }
}

bool FixSessionManager::handleSequenceReset(FixMessage *message)
{
    logInfo("Processing SequenceReset message");

    std::string new_seq_str;
    if (!message->getField(FixFields::NewSeqNo, new_seq_str))
    {
        logError("SequenceReset missing NewSeqNo field");
        return false;
    }

    try
    {
        int new_seq = std::stoi(new_seq_str);
        expected_incoming_seq_num_.store(new_seq);

        logInfo("Reset incoming sequence number to: " + std::to_string(new_seq));
        return true;
    }
    catch (const std::exception &e)
    {
        logError("Invalid new sequence number: " + std::string(e.what()));
        return false;
    }
}

bool FixSessionManager::handleReject(FixMessage *message)
{
    logWarning("Processing Reject message");

    std::string ref_seq_str, text;
    message->getField(FixFields::RefSeqNum, ref_seq_str);
    message->getField(FixFields::Text, text);

    logWarning("Message rejected - RefSeqNum: " + ref_seq_str +
               (text.empty() ? "" : ", Reason: " + text));

    return true;
}

// =================================================================
// MESSAGE SENDERS
// =================================================================

bool FixSessionManager::sendLogon()
{
    FixMessage *logon_msg = createLogonMessage();
    if (!logon_msg)
    {
        logError("Failed to create logon message");
        return false;
    }

    bool success = routeToOutbound(logon_msg, Priority::CRITICAL);
    if (success)
    {
        session_stats_.logons_sent++;
        logDebug("Logon message routed to outbound queue");
    }
    else
    {
        logError("Failed to route logon message");
        // Message will be cleaned up by queue system
    }

    return success;
}

bool FixSessionManager::sendLogout(const std::string &reason)
{
    FixMessage *logout_msg = createLogoutMessage(reason);
    if (!logout_msg)
    {
        logError("Failed to create logout message");
        return false;
    }

    bool success = routeToOutbound(logout_msg, Priority::CRITICAL);
    if (success)
    {
        session_stats_.logouts_sent++;
        logDebug("Logout message routed to outbound queue");
    }
    else
    {
        logError("Failed to route logout message");
        // Message will be cleaned up by queue system
    }

    return success;
}

bool FixSessionManager::sendHeartbeat(const std::string &test_req_id)
{
    FixMessage *heartbeat_msg = createHeartbeatMessage(test_req_id);
    if (!heartbeat_msg)
    {
        logError("Failed to create heartbeat message");
        return false;
    }

    bool success = routeToOutbound(heartbeat_msg, Priority::HIGH);
    if (success)
    {
        session_stats_.heartbeats_sent++;
        last_heartbeat_sent_ = std::chrono::steady_clock::now();
        logDebug("Heartbeat message routed to outbound queue");
    }
    else
    {
        logError("Failed to route heartbeat message");
        // Message will be cleaned up by queue system
    }

    return success;
}

bool FixSessionManager::sendTestRequest()
{
    FixMessage *test_req_msg = createTestRequestMessage();
    if (!test_req_msg)
    {
        logError("Failed to create test request message");
        return false;
    }

    bool success = routeToOutbound(test_req_msg, Priority::HIGH);
    if (success)
    {
        session_stats_.test_requests_sent++;
        test_request_sent_time_ = std::chrono::steady_clock::now();
        logDebug("Test request message routed to outbound queue");
    }
    else
    {
        logError("Failed to route test request message");
        // Message will be cleaned up by queue system
    }

    return success;
}

bool FixSessionManager::sendReject(int ref_seq_num, const std::string &reason)
{
    FixMessage *reject_msg = createRejectMessage(ref_seq_num, reason);
    if (!reject_msg)
    {
        logError("Failed to create reject message");
        return false;
    }

    bool success = routeToOutbound(reject_msg, Priority::MEDIUM);
    if (success)
    {
        session_stats_.rejects_sent++;
        logDebug("Reject message routed to outbound queue");
    }
    else
    {
        logError("Failed to route reject message");
        // Message will be cleaned up by queue system
    }

    return success;
}

bool FixSessionManager::sendSequenceReset(int new_seq_num, bool gap_fill)
{
    FixMessage *reset_msg = createSequenceResetMessage(new_seq_num, gap_fill);
    if (!reset_msg)
    {
        logError("Failed to create sequence reset message");
        return false;
    }

    bool success = routeToOutbound(reset_msg, Priority::CRITICAL);
    if (success)
    {
        session_stats_.sequence_resets_sent++;
        logInfo("Sequence reset sent - NewSeqNum: " + std::to_string(new_seq_num) +
                (gap_fill ? " (gap fill)" : ""));
    }
    else
    {
        logError("Failed to route sequence reset message");
    }

    return success;
}

// =================================================================
// MESSAGE CREATION HELPERS
// =================================================================

FixMessage *FixSessionManager::createLogonMessage()
{
    if (!message_pool_)
    {
        logError("Message pool not set - cannot create logon message");
        return nullptr;
    }

    FixMessage *msg = message_pool_->allocate();
    if (!msg)
    {
        logError("Failed to allocate message from pool");
        return nullptr;
    }

    // Standard header
    msg->setField(FixFields::BeginString, std::string("FIX.4.4"));
    msg->setField(FixFields::MsgType, std::string("A"));
    msg->setField(FixFields::SenderCompID, std::string(config_.sender_comp_id));
    msg->setField(FixFields::TargetCompID, std::string(config_.target_comp_id));
    msg->setField(FixFields::MsgSeqNum, std::to_string(getNextOutgoingSeqNum()));

    // Sending time - simplified format
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y%m%d-%H:%M:%S");
    msg->setField(FixFields::SendingTime, oss.str());

    // Logon-specific fields
    msg->setField(FixFields::HeartBtInt, std::to_string(config_.heartbeat_interval));
    if (config_.reset_sequence_numbers)
    {
        msg->setField(FixFields::ResetSeqNumFlag, std::string("Y"));
    }

    return msg;
}

FixMessage *FixSessionManager::createLogoutMessage(const std::string &reason)
{
    if (!message_pool_)
    {
        logError("Message pool not set - cannot create logout message");
        return nullptr;
    }

    FixMessage *msg = message_pool_->allocate();
    if (!msg)
    {
        logError("Failed to allocate message from pool");
        return nullptr;
    }

    // Standard header
    msg->setField(FixFields::BeginString, std::string("FIX.4.4"));
    msg->setField(FixFields::MsgType, std::string("5"));
    msg->setField(FixFields::SenderCompID, std::string(config_.sender_comp_id));
    msg->setField(FixFields::TargetCompID, std::string(config_.target_comp_id));
    msg->setField(FixFields::MsgSeqNum, std::to_string(getNextOutgoingSeqNum()));

    // Sending time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y%m%d-%H:%M:%S");
    msg->setField(FixFields::SendingTime, oss.str());

    // Logout reason
    if (!reason.empty())
    {
        msg->setField(FixFields::Text, std::string(reason));
    }

    return msg;
}

FixMessage *FixSessionManager::createHeartbeatMessage(const std::string &test_req_id)
{
    if (!message_pool_)
    {
        logError("Message pool not set - cannot create heartbeat message");
        return nullptr;
    }

    FixMessage *msg = message_pool_->allocate();
    if (!msg)
    {
        logError("Failed to allocate message from pool");
        return nullptr;
    }

    // Standard header
    msg->setField(FixFields::BeginString, std::string("FIX.4.4"));
    msg->setField(FixFields::MsgType, std::string("0"));
    msg->setField(FixFields::SenderCompID, std::string(config_.sender_comp_id));
    msg->setField(FixFields::TargetCompID, std::string(config_.target_comp_id));
    msg->setField(FixFields::MsgSeqNum, std::to_string(getNextOutgoingSeqNum()));

    // Sending time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y%m%d-%H:%M:%S");
    msg->setField(FixFields::SendingTime, oss.str());

    // Test request ID if this is a response
    if (!test_req_id.empty())
    {
        msg->setField(FixFields::TestReqID, std::string(test_req_id));
    }

    return msg;
}

FixMessage *FixSessionManager::createTestRequestMessage()
{
    if (!message_pool_)
    {
        logError("Message pool not set - cannot create test request message");
        return nullptr;
    }

    FixMessage *msg = message_pool_->allocate();
    if (!msg)
    {
        logError("Failed to allocate message from pool");
        return nullptr;
    }

    // Standard header
    msg->setField(FixFields::BeginString, std::string("FIX.4.4"));
    msg->setField(FixFields::MsgType, std::string("1"));
    msg->setField(FixFields::SenderCompID, std::string(config_.sender_comp_id));
    msg->setField(FixFields::TargetCompID, std::string(config_.target_comp_id));
    msg->setField(FixFields::MsgSeqNum, std::to_string(getNextOutgoingSeqNum()));

    // Sending time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y%m%d-%H:%M:%S");
    msg->setField(FixFields::SendingTime, oss.str());

    // Generate unique test request ID
    std::string test_req_id = createTestRequestId();
    msg->setField(FixFields::TestReqID, std::string(test_req_id));
    pending_test_request_id_ = test_req_id;

    return msg;
}

FixMessage *FixSessionManager::createRejectMessage(int ref_seq_num, const std::string &reason)
{
    if (!message_pool_)
    {
        logError("Message pool not set - cannot create reject message");
        return nullptr;
    }

    FixMessage *msg = message_pool_->allocate();
    if (!msg)
    {
        logError("Failed to allocate message from pool");
        return nullptr;
    }

    // Standard header
    msg->setField(FixFields::BeginString, std::string("FIX.4.4"));
    msg->setField(FixFields::MsgType, std::string("3"));
    msg->setField(FixFields::SenderCompID, std::string(config_.sender_comp_id));
    msg->setField(FixFields::TargetCompID, std::string(config_.target_comp_id));
    msg->setField(FixFields::MsgSeqNum, std::to_string(getNextOutgoingSeqNum()));

    // Sending time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y%m%d-%H:%M:%S");
    msg->setField(FixFields::SendingTime, oss.str());

    // Reject-specific fields
    msg->setField(FixFields::RefSeqNum, std::to_string(ref_seq_num));
    if (!reason.empty())
    {
        msg->setField(FixFields::Text, std::string(reason));
    }

    return msg;
}

FixMessage *FixSessionManager::createResendRequestMessage(int begin_seq, int end_seq)
{
    if (!message_pool_)
    {
        logError("Message pool not set - cannot create resend request message");
        return nullptr;
    }

    FixMessage *msg = message_pool_->allocate();
    if (!msg)
    {
        logError("Failed to allocate message from pool");
        return nullptr;
    }

    // Standard header
    msg->setField(FixFields::BeginString, std::string("FIX.4.4"));
    msg->setField(FixFields::MsgType, std::string("2"));
    msg->setField(FixFields::SenderCompID, std::string(config_.sender_comp_id));
    msg->setField(FixFields::TargetCompID, std::string(config_.target_comp_id));
    msg->setField(FixFields::MsgSeqNum, std::to_string(getNextOutgoingSeqNum()));

    // Sending time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y%m%d-%H:%M:%S");
    msg->setField(FixFields::SendingTime, oss.str());

    // Resend request-specific fields
    msg->setField(FixFields::BeginSeqNo, std::to_string(begin_seq));
    msg->setField(FixFields::EndSeqNo, std::to_string(end_seq));

    return msg;
}

FixMessage *FixSessionManager::createSequenceResetMessage(int new_seq_num, bool gap_fill)
{
    if (!message_pool_)
    {
        logError("Message pool not set - cannot create sequence reset message");
        return nullptr;
    }

    FixMessage *msg = message_pool_->allocate();
    if (!msg)
    {
        logError("Failed to allocate message from pool");
        return nullptr;
    }

    // Standard header
    msg->setField(FixFields::BeginString, std::string("FIX.4.4"));
    msg->setField(FixFields::MsgType, std::string("4"));
    msg->setField(FixFields::SenderCompID, std::string(config_.sender_comp_id));
    msg->setField(FixFields::TargetCompID, std::string(config_.target_comp_id));
    msg->setField(FixFields::MsgSeqNum, std::to_string(getNextOutgoingSeqNum()));

    // Sending time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y%m%d-%H:%M:%S");
    msg->setField(FixFields::SendingTime, oss.str());

    // Sequence reset-specific fields
    msg->setField(FixFields::NewSeqNo, std::to_string(new_seq_num));
    if (gap_fill)
    {
        msg->setField(FixFields::GapFillFlag, std::string("Y"));
    }

    return msg;
}

// =================================================================
// VALIDATION METHODS
// =================================================================

bool FixSessionManager::validateSequenceNumber(const FixMessage *message)
{
    std::string seq_num_str;
    if (!message->getField(FixFields::MsgSeqNum, seq_num_str))
    {
        logError("Message missing sequence number");
        return false;
    }

    try
    {
        int received_seq = std::stoi(seq_num_str);
        int expected_seq = expected_incoming_seq_num_.load();

        // Check if this is a resent message with PossDupFlag
        std::string poss_dup_flag;
        bool is_resend = message->getField(FixFields::PossDupFlag, poss_dup_flag) && poss_dup_flag == "Y";

        if (received_seq == expected_seq)
        {
            // Correct sequence number - increment expected
            expected_incoming_seq_num_.store(expected_seq + 1);
            return true;
        }
        else if (received_seq > expected_seq)
        {
            if (is_resend)
            {
                // This is a resent message filling our gap - accept it and update expected
                logInfo("Received resent message filling gap - seq: " + std::to_string(received_seq));
                expected_incoming_seq_num_.store(received_seq + 1);
                return true;
            }
            else
            {
                // Gap detected - request resend
                logWarning("Sequence number gap detected - expected: " +
                           std::to_string(expected_seq) + ", received: " +
                           std::to_string(received_seq));
                handleSequenceNumberGap(expected_seq, received_seq);
                return false;
            }
        }
        else
        {
            if (is_resend)
            {
                // Valid resent message - accept but don't update sequence
                logDebug("Received valid resent message - seq: " + std::to_string(received_seq));
                return true;
            }
            else
            {
                // Duplicate or old message without PossDupFlag
                logWarning("Old/duplicate sequence number - expected: " +
                           std::to_string(expected_seq) + ", received: " +
                           std::to_string(received_seq));
                return false;
            }
        }
    }
    catch (const std::exception &e)
    {
        logError("Invalid sequence number format: " + seq_num_str);
        return false;
    }
}

void FixSessionManager::handleSequenceNumberGap(int expected, int received)
{
    logInfo("Handling sequence number gap: " + std::to_string(expected) +
            " to " + std::to_string(received - 1));

    const int begin_seq = expected;
    const int end_seq = received - 1;

    FixMessage *msg = createResendRequestMessage(begin_seq, end_seq);

    bool success = routeToOutbound(msg, Priority::CRITICAL);
    if (success)
    {
        logInfo("Resend request sent to broker for gap fill");
    }
    else
    {
        logError("Failed to route resend request message");
    }
}

bool FixSessionManager::validateSessionMessage(const FixMessage *message) const
{
    std::string sender_comp_id, target_comp_id;

    if (!message->getField(FixFields::SenderCompID, sender_comp_id) ||
        !message->getField(FixFields::TargetCompID, target_comp_id))
    {
        return false;
    }

    return isValidSenderCompId(sender_comp_id) && isValidTargetCompId(target_comp_id);
}

bool FixSessionManager::isValidSenderCompId(const std::string &sender_comp_id) const
{
    return sender_comp_id == config_.target_comp_id;
}

bool FixSessionManager::isValidTargetCompId(const std::string &target_comp_id) const
{
    return target_comp_id == config_.sender_comp_id;
}

// =================================================================
// HEARTBEAT MANAGEMENT
// =================================================================

void FixSessionManager::startHeartbeatTimer()
{
    if (heartbeat_timer_running_.load())
    {
        return; // Already running
    }

    heartbeat_timer_running_.store(true);
    heartbeat_thread_ = std::thread(&FixSessionManager::heartbeatTimerFunction, this);
    logDebug("Heartbeat timer started");
}

void FixSessionManager::stopHeartbeatTimer()
{
    if (!heartbeat_timer_running_.load())
    {
        return; // Not running
    }

    heartbeat_timer_running_.store(false);

    if (heartbeat_thread_.joinable())
    {
        heartbeat_thread_.join();
    }

    logDebug("Heartbeat timer stopped");
}

void FixSessionManager::heartbeatTimerFunction()
{
    logDebug("Heartbeat timer thread started");

    while (heartbeat_timer_running_.load() && session_state_.load() == SessionState::LOGGED_ON)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (shouldSendHeartbeat())
        {
            bool sent = sendHeartbeat();
            if (!sent)
            {
                logError("Failed to send heartbeat");
            }
        }

        if (shouldSendTestRequest())
        {
            bool sent = sendTestRequest();
            if (!sent)
            {
                logError("Failed to send test request");
            }
        }
    }

    logDebug("Heartbeat timer thread ended");
}

bool FixSessionManager::shouldSendHeartbeat() const
{
    auto now = std::chrono::steady_clock::now();
    auto time_since_last_heartbeat = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_heartbeat_sent_);

    return time_since_last_heartbeat.count() >= config_.heartbeat_interval;
}

bool FixSessionManager::shouldSendTestRequest() const
{
    auto now = std::chrono::steady_clock::now();
    auto time_since_last_message = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_message_received_);

    // Send test request if we haven't received any message for 2x heartbeat interval
    return time_since_last_message.count() >= (config_.heartbeat_interval * 2);
}

// =================================================================
// UTILITY METHODS
// =================================================================

void FixSessionManager::updateSessionState(SessionState new_state)
{
    SessionState old_state = session_state_.exchange(new_state);
    session_stats_.current_state = new_state;

    logInfo("Session state changed: " + std::to_string(static_cast<int>(old_state)) +
            " -> " + std::to_string(static_cast<int>(new_state)));
}

std::string FixSessionManager::createTestRequestId()
{
    uint64_t counter = test_req_id_counter_++;
    return "TEST_" + std::to_string(counter);
}