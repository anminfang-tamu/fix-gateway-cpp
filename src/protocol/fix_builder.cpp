#include "protocol/fix_builder.h"
#include "protocol/fix_fields.h"
#include "utils/logger.h"
#include "utils/performance_timer.h"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace fix_gateway::protocol
{
    using FixMessagePtr = std::shared_ptr<FixMessage>;
    using namespace fix_gateway::protocol::FixFields;
    using namespace fix_gateway::protocol::MsgTypes;

    // =================================================================
    // BuilderStats Implementation
    // =================================================================

    void FixBuilder::BuilderStats::updateTiming(uint64_t buildTimeNanos)
    {
        totalBuildTimeNanos += buildTimeNanos;
        lastBuildTimeNanos = buildTimeNanos;

        if (messagesBuildSuccess > 0)
        {
            averageBuildTimeNanos = totalBuildTimeNanos / messagesBuildSuccess;
        }
    }

    void FixBuilder::BuilderStats::reset()
    {
        messagesBuildAttempts = 0;
        messagesBuildSuccess = 0;
        messagesBuildFailure = 0;
        totalBuildTimeNanos = 0;
        averageBuildTimeNanos = 0;
        lastBuildTimeNanos = 0;
    }

    // =================================================================
    // FixBuilder Constructor and Configuration
    // =================================================================

    FixBuilder::FixBuilder(const BuilderConfig &config)
        : config_(config), nextSeqNum_(1)
    {
        if (config_.senderCompID.empty() || config_.targetCompID.empty())
        {
            throw std::invalid_argument("SenderCompID and TargetCompID must be provided");
        }
    }

    FixBuilder::FixBuilder(const std::string &senderCompID, const std::string &targetCompID)
        : nextSeqNum_(1)
    {
        config_.senderCompID = senderCompID;
        config_.targetCompID = targetCompID;
    }

    void FixBuilder::updateConfig(const BuilderConfig &config)
    {
        config_ = config;
    }

    void FixBuilder::setSenderCompID(const std::string &senderID)
    {
        config_.senderCompID = senderID;
    }

    void FixBuilder::setTargetCompID(const std::string &targetID)
    {
        config_.targetCompID = targetID;
    }

    // =================================================================
    // Core Building Methods
    // =================================================================

    std::string FixBuilder::buildMessage(const FixMessage &message)
    {
        startTiming();
        stats_.messagesBuildAttempts++;

        try
        {
            std::string result = buildImpl(message);
            stats_.messagesBuildSuccess++;
            endTiming();
            return result;
        }
        catch (const std::exception &e)
        {
            stats_.messagesBuildFailure++;
            endTiming();
            throw;
        }
    }

    std::string FixBuilder::buildMessage(FixMessagePtr message)
    {
        if (!message)
        {
            throw std::invalid_argument("FixMessage pointer cannot be null");
        }
        return buildMessage(*message);
    }

    bool FixBuilder::buildMessage(const FixMessage &message, std::string &output)
    {
        try
        {
            output = buildMessage(message);
            return true;
        }
        catch (const std::exception &e)
        {
            return false;
        }
    }

    // =================================================================
    // Core Building Implementation
    // =================================================================

    std::string FixBuilder::buildImpl(const FixMessage &message)
    {
        // Create a working copy of the message
        FixMessage workingMessage = message;

        // Add standard header
        addStandardHeader(workingMessage, workingMessage.getMsgType());

        // Add standard trailer
        addStandardTrailer(workingMessage);

        // Order fields if required
        if (config_.enforceFieldOrder)
        {
            orderFields(workingMessage);
        }

        // Calculate body length and checksum
        if (config_.autoBodyLength || config_.autoChecksum)
        {
            workingMessage.updateLengthAndChecksum();
        }

        return workingMessage.toString();
    }

    void FixBuilder::addStandardHeader(FixMessage &message, const std::string &msgType)
    {
        // BeginString (8)
        message.setField(FixFields::BeginString, config_.beginString);

        // BodyLength (9) - will be calculated later if auto enabled
        if (!config_.autoBodyLength && !message.hasField(FixFields::BodyLength))
        {
            message.setField(FixFields::BodyLength, "0");
        }

        // MsgType (35)
        if (!msgType.empty())
        {
            message.setField(FixFields::MsgType, msgType);
        }

        // SenderCompID (49)
        if (!config_.senderCompID.empty())
        {
            message.setField(FixFields::SenderCompID, config_.senderCompID);
        }

        // TargetCompID (56)
        if (!config_.targetCompID.empty())
        {
            message.setField(FixFields::TargetCompID, config_.targetCompID);
        }

        // MsgSeqNum (34)
        if (config_.autoSequenceNumber)
        {
            message.setField(FixFields::MsgSeqNum, nextSeqNum_++);
        }

        // SendingTime (52)
        if (config_.autoTimestamp)
        {
            message.setSendingTime();
        }
    }

    void FixBuilder::addStandardTrailer(FixMessage &message)
    {
        // CheckSum (10) - will be calculated later if auto enabled
        if (!config_.autoChecksum && !message.hasField(FixFields::CheckSum))
        {
            message.setField(FixFields::CheckSum, "000");
        }
    }

    void FixBuilder::orderFields(FixMessage &message) const
    {
        // For now, we'll rely on FixMessage's natural ordering
        // A more sophisticated implementation would reorder fields
    }

    std::vector<int> FixBuilder::getHeaderFieldOrder() const
    {
        return {
            FixFields::BeginString,  // 8
            FixFields::BodyLength,   // 9
            FixFields::MsgType,      // 35
            FixFields::SenderCompID, // 49
            FixFields::TargetCompID, // 56
            FixFields::MsgSeqNum,    // 34
            FixFields::SendingTime   // 52
        };
    }

    std::vector<int> FixBuilder::getTrailerFieldOrder() const
    {
        return {
            FixFields::CheckSum // 10
        };
    }

    std::vector<int> FixBuilder::getBodyFieldOrder(const std::string &msgType) const
    {
        // Message-specific field ordering
        if (msgType == MsgTypes::NewOrderSingle)
        {
            return {
                FixFields::ClOrdID,    // 11
                FixFields::Symbol,     // 55
                FixFields::Side,       // 54
                FixFields::OrderQty,   // 38
                FixFields::OrdType,    // 40
                FixFields::Price,      // 44
                FixFields::TimeInForce // 59
            };
        }

        // Default ordering (no specific order)
        return {};
    }

    // =================================================================
    // Performance Helpers
    // =================================================================

    void FixBuilder::startTiming()
    {
        buildStartTime_ = std::chrono::steady_clock::now();
    }

    void FixBuilder::endTiming()
    {
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            endTime - buildStartTime_)
                            .count();
        stats_.updateTiming(static_cast<uint64_t>(duration));
    }

    // =================================================================
    // Session-Level Messages
    // =================================================================

    std::string FixBuilder::buildLogon(int heartBeatInterval, int encryptMethod)
    {
        FixMessage message;
        message.setField(FixFields::MsgType, MsgTypes::Logon);
        message.setField(FixFields::EncryptMethod, encryptMethod);
        message.setField(FixFields::HeartBtInt, heartBeatInterval);

        return buildMessage(message);
    }

    std::string FixBuilder::buildLogout(const std::string &text)
    {
        FixMessage message;
        message.setField(FixFields::MsgType, MsgTypes::Logout);

        if (!text.empty())
        {
            message.setField(FixFields::Text, text);
        }

        return buildMessage(message);
    }

    std::string FixBuilder::buildHeartbeat(const std::string &testReqID)
    {
        FixMessage message;
        message.setField(FixFields::MsgType, MsgTypes::Heartbeat);

        if (!testReqID.empty())
        {
            message.setField(FixFields::TestReqID, testReqID);
        }

        return buildMessage(message);
    }

    std::string FixBuilder::buildTestRequest(const std::string &testReqID)
    {
        FixMessage message;
        message.setField(FixFields::MsgType, MsgTypes::TestRequest);
        message.setField(FixFields::TestReqID, testReqID);

        return buildMessage(message);
    }

    std::string FixBuilder::buildResendRequest(int beginSeqNo, int endSeqNo)
    {
        FixMessage message;
        message.setField(FixFields::MsgType, MsgTypes::ResendRequest);
        message.setField(FixFields::BeginSeqNo, beginSeqNo);

        if (endSeqNo > 0)
        {
            message.setField(FixFields::EndSeqNo, endSeqNo);
        }
        else
        {
            message.setField(FixFields::EndSeqNo, 0); // 0 = infinity
        }

        return buildMessage(message);
    }

    std::string FixBuilder::buildSequenceReset(int newSeqNo, bool gapFillFlag)
    {
        FixMessage message;
        message.setField(FixFields::MsgType, MsgTypes::SequenceReset);
        message.setField(FixFields::NewSeqNo, newSeqNo);

        if (gapFillFlag)
        {
            message.setField(FixFields::GapFillFlag, "Y");
        }

        return buildMessage(message);
    }

    std::string FixBuilder::buildReject(int refSeqNum, const std::string &refMsgType, const std::string &reason)
    {
        FixMessage message;
        message.setField(FixFields::MsgType, MsgTypes::Reject);
        message.setField(FixFields::RefSeqNum, refSeqNum);

        if (!refMsgType.empty())
        {
            message.setField(FixFields::RefMsgType, refMsgType);
        }

        if (!reason.empty())
        {
            message.setField(FixFields::Text, reason);
        }

        return buildMessage(message);
    }

    // =================================================================
    // Trading Messages
    // =================================================================

    std::string FixBuilder::buildNewOrderSingle(const std::string &clOrdID,
                                                const std::string &symbol,
                                                const std::string &side,
                                                const std::string &orderQty,
                                                const std::string &price,
                                                const std::string &orderType,
                                                const std::string &timeInForce,
                                                const std::string &account)
    {
        FixMessage message;
        message.setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
        message.setField(FixFields::ClOrdID, clOrdID);
        message.setField(FixFields::Symbol, symbol);
        message.setField(FixFields::Side, side);
        message.setField(FixFields::OrderQty, orderQty);
        message.setField(FixFields::OrdType, orderType);

        if (orderType == OrderType::Limit || orderType == OrderType::StopLimit)
        {
            message.setField(FixFields::Price, price);
        }

        message.setField(FixFields::TimeInForce, timeInForce);

        if (!account.empty())
        {
            message.setField(FixFields::Account, account);
        }

        return buildMessage(message);
    }

    std::string FixBuilder::buildExecutionReport(const std::string &orderID,
                                                 const std::string &execID,
                                                 const std::string &execType,
                                                 const std::string &ordStatus,
                                                 const std::string &symbol,
                                                 const std::string &side,
                                                 const std::string &orderQty,
                                                 const std::string &lastQty,
                                                 const std::string &lastPx,
                                                 const std::string &leavesQty,
                                                 const std::string &cumQty,
                                                 const std::string &avgPx)
    {
        FixMessage message;
        message.setField(FixFields::MsgType, MsgTypes::ExecutionReport);
        message.setField(FixFields::OrderID, orderID);
        message.setField(FixFields::ExecID, execID);
        message.setField(FixFields::ExecType, execType);
        message.setField(FixFields::OrdStatus, ordStatus);
        message.setField(FixFields::Symbol, symbol);
        message.setField(FixFields::Side, side);
        message.setField(FixFields::OrderQty, orderQty);
        message.setField(FixFields::LastQty, lastQty);
        message.setField(FixFields::LastPx, lastPx);
        message.setField(FixFields::LeavesQty, leavesQty);
        message.setField(FixFields::CumQty, cumQty);
        message.setField(FixFields::AvgPx, avgPx);

        return buildMessage(message);
    }

    // =================================================================
    // MessageBuilder Implementation
    // =================================================================

    FixBuilder::MessageBuilder::MessageBuilder(FixBuilder &parent, const std::string &msgType)
        : parent_(parent)
    {
        message_.setField(FixFields::MsgType, msgType);
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setField(int tag, const std::string &value)
    {
        message_.setField(tag, value);
        return *this;
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setField(int tag, int value)
    {
        message_.setField(tag, value);
        return *this;
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setField(int tag, double value, int precision)
    {
        message_.setField(tag, value, precision);
        return *this;
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setField(int tag, char value)
    {
        message_.setField(tag, value);
        return *this;
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setClOrdID(const std::string &clOrdID)
    {
        return setField(FixFields::ClOrdID, clOrdID);
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setSymbol(const std::string &symbol)
    {
        return setField(FixFields::Symbol, symbol);
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setSide(const std::string &side)
    {
        return setField(FixFields::Side, side);
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setOrderQty(const std::string &qty)
    {
        return setField(FixFields::OrderQty, qty);
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setPrice(const std::string &price)
    {
        return setField(FixFields::Price, price);
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setAccount(const std::string &account)
    {
        return setField(FixFields::Account, account);
    }

    FixBuilder::MessageBuilder &FixBuilder::MessageBuilder::setText(const std::string &text)
    {
        return setField(FixFields::Text, text);
    }

    std::string FixBuilder::MessageBuilder::build()
    {
        return parent_.buildMessage(message_);
    }

    FixMessagePtr FixBuilder::MessageBuilder::buildMessage()
    {
        return std::make_shared<FixMessage>(message_);
    }

    FixBuilder::MessageBuilder FixBuilder::createMessage(const std::string &msgType)
    {
        return MessageBuilder(*this, msgType);
    }

    // =================================================================
    // Validation
    // =================================================================

    bool FixBuilder::validateMessage(const FixMessage &message) const
    {
        // Basic validation
        if (!message.hasField(FixFields::MsgType))
        {
            return false;
        }

        // Check required header fields for FIX 4.4
        std::vector<int> requiredFields = {
            FixFields::BeginString,
            FixFields::BodyLength,
            FixFields::MsgType,
            FixFields::SenderCompID,
            FixFields::TargetCompID,
            FixFields::MsgSeqNum,
            FixFields::SendingTime};

        for (int field : requiredFields)
        {
            if (!message.hasField(field))
            {
                return false;
            }
        }

        return true;
    }

    std::vector<std::string> FixBuilder::getValidationErrors(const FixMessage &message) const
    {
        std::vector<std::string> errors;

        if (!message.hasField(FixFields::MsgType))
        {
            errors.push_back("Missing MsgType field (35)");
        }

        if (!message.hasField(FixFields::BeginString))
        {
            errors.push_back("Missing BeginString field (8)");
        }

        if (!message.hasField(FixFields::SenderCompID))
        {
            errors.push_back("Missing SenderCompID field (49)");
        }

        if (!message.hasField(FixFields::TargetCompID))
        {
            errors.push_back("Missing TargetCompID field (56)");
        }

        return errors;
    }

} // namespace fix_gateway::protocol