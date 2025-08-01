#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <memory>

namespace fix_gateway::protocol
{
    // Constructor implementations
    FixMessage::FixMessage()
        : creationTime_(std::chrono::steady_clock::now()),
          lastModified_(creationTime_)
    {
        // Initialize with current timestamp
        setSendingTime();
    }

    FixMessage::FixMessage(const std::string &rawMessage)
        : FixMessage()
    {
        // TODO: need to implement a FIX parser
        if (!rawMessage.empty())
        {
            parseFromString(rawMessage);
        }
    }

    FixMessage::FixMessage(const FieldMap &fields)
        : fields_(fields),
          creationTime_(std::chrono::steady_clock::now()),
          lastModified_(creationTime_)
    {
    }

    // Copy constructor
    FixMessage::FixMessage(const FixMessage &other)
        : fields_(other.fields_),
          creationTime_(other.creationTime_),
          lastModified_(std::chrono::steady_clock::now()),
          processingStart_(other.processingStart_),
          processingEnd_(other.processingEnd_)
    {
        // Cache is not copied - will be regenerated as needed
    }

    // Move constructor
    FixMessage::FixMessage(FixMessage &&other) noexcept
        : fields_(std::move(other.fields_)),
          creationTime_(other.creationTime_),
          lastModified_(other.lastModified_),
          processingStart_(other.processingStart_),
          processingEnd_(other.processingEnd_)
    {
        // Move cached data
        checksumValid_ = other.checksumValid_;
        lengthValid_ = other.lengthValid_;
        cachedString_ = std::move(other.cachedString_);
        stringCacheValid_ = other.stringCacheValid_;

        // Invalidate other's cache
        other.invalidateCache();
    }

    // Assignment operators
    FixMessage &FixMessage::operator=(const FixMessage &other)
    {
        if (this != &other)
        {
            fields_ = other.fields_;
            creationTime_ = other.creationTime_;
            processingStart_ = other.processingStart_;
            processingEnd_ = other.processingEnd_;
            touchModified();
            invalidateCache();
        }
        return *this;
    }

    FixMessage &FixMessage::operator=(FixMessage &&other) noexcept
    {
        if (this != &other)
        {
            fields_ = std::move(other.fields_);
            creationTime_ = other.creationTime_;
            lastModified_ = other.lastModified_;
            processingStart_ = other.processingStart_;
            processingEnd_ = other.processingEnd_;

            // Move cached data
            checksumValid_ = other.checksumValid_;
            lengthValid_ = other.lengthValid_;
            cachedString_ = std::move(other.cachedString_);
            stringCacheValid_ = other.stringCacheValid_;

            // Invalidate other
            other.invalidateCache();
        }
        return *this;
    }

    // Field operations
    void FixMessage::setField(int tag, const std::string &value)
    {
        setFieldInternal(tag, value);
    }

    void FixMessage::setField(int tag, int value)
    {
        setFieldInternal(tag, std::to_string(value));
    }

    void FixMessage::setField(int tag, double value, int precision)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << value;
        setFieldInternal(tag, oss.str());
    }

    void FixMessage::setField(int tag, char value)
    {
        setFieldInternal(tag, std::string(1, value));
    }

    bool FixMessage::getField(int tag, std::string &value) const
    {
        auto it = fields_.find(tag);
        if (it != fields_.end())
        {
            value = it->second;
            return true;
        }
        return false;
    }

    bool FixMessage::getField(int tag, int &value) const
    {
        std::string strValue;
        if (getField(tag, strValue))
        {
            try
            {
                value = std::stoi(strValue);
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        }
        return false;
    }

    bool FixMessage::getField(int tag, double &value) const
    {
        std::string strValue;
        if (getField(tag, strValue))
        {
            try
            {
                value = std::stod(strValue);
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        }
        return false;
    }

    bool FixMessage::getField(int tag, char &value) const
    {
        std::string strValue;
        if (getField(tag, strValue) && !strValue.empty())
        {
            value = strValue[0];
            return true;
        }
        return false;
    }

    const std::string *FixMessage::getFieldPtr(int tag) const
    {
        auto it = fields_.find(tag);
        return (it != fields_.end()) ? &it->second : nullptr;
    }

    bool FixMessage::hasField(int tag) const
    {
        return fields_.find(tag) != fields_.end();
    }

    void FixMessage::removeField(int tag)
    {
        if (fields_.erase(tag) > 0)
        {
            touchModified();
            invalidateCache();
        }
    }

    // Common field accessors
    int FixMessage::getMsgSeqNum() const
    {
        int seqNum = 0;
        getField(FixFields::MsgSeqNum, seqNum);
        return seqNum;
    }

    // Ultra-fast message type classification
    FixMsgType FixMessage::getMsgTypeEnum() const
    {
        // Check if already cached
        if (msgTypeCached_)
        {
            return cachedMsgType_;
        }

        // Get message type string pointer (no allocation)
        const std::string *msgTypePtr = getFieldPtr(FixFields::MsgType);
        if (!msgTypePtr)
        {
            cachedMsgType_ = FixMsgType::UNKNOWN;
            msgTypeCached_ = true;
            return cachedMsgType_;
        }

        // Convert string to enum using ultra-fast character comparison
        cachedMsgType_ = FixMsgTypeUtils::fromString(msgTypePtr->c_str());
        msgTypeCached_ = true;

        return cachedMsgType_;
    }

    // Session-level field setters
    void FixMessage::setSenderCompID(const std::string &senderID)
    {
        setField(FixFields::SenderCompID, senderID);
    }

    void FixMessage::setTargetCompID(const std::string &targetID)
    {
        setField(FixFields::TargetCompID, targetID);
    }

    void FixMessage::setMsgSeqNum(int seqNum)
    {
        setField(FixFields::MsgSeqNum, seqNum);
    }

    void FixMessage::setSendingTime()
    {
        setSendingTime(std::chrono::system_clock::now());
    }

    void FixMessage::setSendingTime(const std::chrono::system_clock::time_point &time)
    {
        // Format time as YYYYMMDD-HH:MM:SS.sss (FIX UTCTimestamp format)
        auto timeT = std::chrono::system_clock::to_time_t(time);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      time.time_since_epoch()) %
                  1000;

        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&timeT), "%Y%m%d-%H:%M:%S");
        oss << "." << std::setfill('0') << std::setw(3) << ms.count();

        setField(FixFields::SendingTime, oss.str());
    }

    // Message validation
    bool FixMessage::isValid() const
    {
        return validate().empty();
    }

    bool FixMessage::isAdminMessage() const
    {
        std::string msgType = getMsgType();
        return FixMessageUtils::isSessionMessage(msgType);
    }

    bool FixMessage::isApplicationMessage() const
    {
        return !isAdminMessage();
    }

    std::vector<std::string> FixMessage::validate() const
    {
        std::vector<std::string> errors;

        // Check required session fields
        if (!hasRequiredSessionFields())
        {
            errors.push_back("Missing required session fields");
        }

        // Validate message format
        if (!hasValidFormat())
        {
            errors.push_back("Invalid message format");
        }

        // Validate specific field values
        std::string msgType = getMsgType();
        if (msgType.empty())
        {
            errors.push_back("Missing MsgType field");
        }

        // Validate trading-specific fields if it's an application message
        if (isApplicationMessage())
        {
            if (msgType == MsgTypes::NewOrderSingle)
            {
                if (getClOrdID().empty())
                    errors.push_back("Missing ClOrdID");
                if (getSymbol().empty())
                    errors.push_back("Missing Symbol");
                if (getSide().empty())
                    errors.push_back("Missing Side");
                if (getOrderQty().empty())
                    errors.push_back("Missing OrderQty");
            }
        }

        return errors;
    }

    // Serialization
    std::string FixMessage::toString() const
    {
        if (stringCacheValid_)
        {
            return cachedString_;
        }

        std::ostringstream oss;

        // Header fields first (BeginString, BodyLength, MsgType)
        if (hasField(FixFields::BeginString))
        {
            oss << FixFields::BeginString << "=" << getFieldValue(FixFields::BeginString) << FIX_SOH;
        }
        else
        {
            oss << FixFields::BeginString << "=" << FIX_VERSION_44 << FIX_SOH;
        }

        // Calculate and add body length
        size_t bodyLength = calculateBodyLength();
        oss << FixFields::BodyLength << "=" << bodyLength << FIX_SOH;

        // Add MsgType
        oss << FixFields::MsgType << "=" << getMsgType() << FIX_SOH;

        // Add all other fields (except BeginString, BodyLength, MsgType, CheckSum)
        for (const auto &field : fields_)
        {
            int tag = field.first;
            if (tag != FixFields::BeginString &&
                tag != FixFields::BodyLength &&
                tag != FixFields::MsgType &&
                tag != FixFields::CheckSum)
            {
                oss << tag << "=" << field.second << FIX_SOH;
            }
        }

        // Calculate and add checksum
        std::string messageWithoutChecksum = oss.str();
        std::string checksum = calculateChecksum();
        oss << FixFields::CheckSum << "=" << checksum << FIX_SOH;

        cachedString_ = oss.str();
        stringCacheValid_ = true;

        return cachedString_;
    }

    std::string FixMessage::toStringWithoutChecksum() const
    {
        std::ostringstream oss;

        // Add all fields except checksum
        for (const auto &field : fields_)
        {
            if (field.first != FixFields::CheckSum)
            {
                oss << field.first << "=" << field.second << FIX_SOH;
            }
        }

        return oss.str();
    }

    size_t FixMessage::calculateBodyLength() const
    {
        size_t length = 0;

        // Count all fields except BeginString, BodyLength, and CheckSum
        for (const auto &field : fields_)
        {
            int tag = field.first;
            if (tag != FixFields::BeginString &&
                tag != FixFields::BodyLength &&
                tag != FixFields::CheckSum)
            {

                // tag=value<SOH>
                length += std::to_string(tag).length() + 1 + field.second.length() + 1;
            }
        }

        return length;
    }

    std::string FixMessage::calculateChecksum() const
    {
        std::string messageWithoutChecksum = toStringWithoutChecksum();
        return FixMessageUtils::calculateChecksum(messageWithoutChecksum);
    }

    void FixMessage::updateLengthAndChecksum()
    {
        // This will be handled automatically in toString()
        invalidateCache();
    }

    // Performance monitoring
    void FixMessage::markProcessingStart()
    {
        processingStart_ = std::chrono::steady_clock::now();
    }

    void FixMessage::markProcessingEnd()
    {
        processingEnd_ = std::chrono::steady_clock::now();
    }

    std::chrono::nanoseconds FixMessage::getProcessingLatency() const
    {
        if (processingStart_.time_since_epoch().count() > 0 &&
            processingEnd_.time_since_epoch().count() > 0)
        {
            return processingEnd_ - processingStart_;
        }
        return std::chrono::nanoseconds{0};
    }

    // Debug and logging
    std::string FixMessage::toFormattedString() const
    {
        std::ostringstream oss;
        oss << "FixMessage {\n";

        for (const auto &field : fields_)
        {
            oss << "  " << field.first << " ("
                << FieldNames::getFieldName(field.first) << "): "
                << field.second << "\n";
        }

        oss << "}";
        return oss.str();
    }

    std::string FixMessage::getFieldsSummary() const
    {
        std::ostringstream oss;
        oss << "MsgType=" << getMsgType();

        if (!getClOrdID().empty())
            oss << " ClOrdID=" << getClOrdID();
        if (!getSymbol().empty())
            oss << " Symbol=" << getSymbol();
        if (!getSide().empty())
            oss << " Side=" << getSide();
        if (getMsgSeqNum() > 0)
            oss << " MsgSeqNum=" << getMsgSeqNum();

        return oss.str();
    }

    // Private helper methods
    std::string FixMessage::getFieldValue(int tag) const
    {
        auto it = fields_.find(tag);
        return (it != fields_.end()) ? it->second : "";
    }

    void FixMessage::setFieldInternal(int tag, const std::string &value)
    {
        fields_[tag] = value;
        touchModified();
        invalidateCache();
    }

    void FixMessage::invalidateCache()
    {
        stringCacheValid_ = false;
        checksumValid_ = false;
        lengthValid_ = false;
        cachedString_.clear();

        // Invalidate message type cache (Option 3 optimization)
        msgTypeCached_ = false;
        cachedMsgType_ = FixMsgType::UNKNOWN;
    }

    void FixMessage::touchModified()
    {
        lastModified_ = std::chrono::steady_clock::now();
    }

    bool FixMessage::hasRequiredSessionFields() const
    {
        return hasField(FixFields::MsgType) &&
               hasField(FixFields::SenderCompID) &&
               hasField(FixFields::TargetCompID);
    }

    bool FixMessage::hasValidFormat() const
    {
        // Basic format validation
        return !fields_.empty();
    }

    void FixMessage::parseFromString(const std::string &rawMessage)
    {
        // Simple parsing implementation for demonstration
        size_t pos = 0;
        while (pos < rawMessage.length())
        {
            size_t eqPos = rawMessage.find('=', pos);
            if (eqPos == std::string::npos)
                break;

            size_t sohPos = rawMessage.find(FIX_SOH, eqPos);
            if (sohPos == std::string::npos)
                sohPos = rawMessage.length();

            try
            {
                int tag = std::stoi(rawMessage.substr(pos, eqPos - pos));
                std::string value = rawMessage.substr(eqPos + 1, sohPos - eqPos - 1);
                fields_[tag] = value;
            }
            catch (const std::exception &)
            {
                // Skip invalid field
            }

            pos = sohPos + 1;
        }

        touchModified();
        invalidateCache();
    }

    // FieldNames implementation
    std::unordered_map<int, std::string> FieldNames::tagToName_;
    std::unordered_map<std::string, int> FieldNames::nameToTag_;

    std::string FieldNames::getFieldName(int fieldTag)
    {
        if (tagToName_.empty())
        {
            initializeMaps();
        }

        auto it = tagToName_.find(fieldTag);
        return (it != tagToName_.end()) ? it->second : "Unknown";
    }

    int FieldNames::getFieldTag(const std::string &fieldName)
    {
        if (nameToTag_.empty())
        {
            initializeMaps();
        }

        auto it = nameToTag_.find(fieldName);
        return (it != nameToTag_.end()) ? it->second : 0;
    }

    void FieldNames::initializeMaps()
    {
        // Initialize common field mappings
        tagToName_[FixFields::BeginString] = "BeginString";
        tagToName_[FixFields::BodyLength] = "BodyLength";
        tagToName_[FixFields::CheckSum] = "CheckSum";
        tagToName_[FixFields::MsgType] = "MsgType";
        tagToName_[FixFields::MsgSeqNum] = "MsgSeqNum";
        tagToName_[FixFields::SenderCompID] = "SenderCompID";
        tagToName_[FixFields::TargetCompID] = "TargetCompID";
        tagToName_[FixFields::SendingTime] = "SendingTime";

        // Trading fields
        tagToName_[FixFields::ClOrdID] = "ClOrdID";
        tagToName_[FixFields::OrderID] = "OrderID";
        tagToName_[FixFields::Symbol] = "Symbol";
        tagToName_[FixFields::Side] = "Side";
        tagToName_[FixFields::OrderQty] = "OrderQty";
        tagToName_[FixFields::Price] = "Price";
        tagToName_[FixFields::OrdType] = "OrdType";
        tagToName_[FixFields::TimeInForce] = "TimeInForce";
        tagToName_[FixFields::ExecType] = "ExecType";
        tagToName_[FixFields::OrdStatus] = "OrdStatus";

        // Build reverse mapping
        for (const auto &pair : tagToName_)
        {
            nameToTag_[pair.second] = pair.first;
        }
    }

    // Utility functions implementation
    namespace FixMessageUtils
    {
        bool isSessionMessage(const std::string &msgType)
        {
            return msgType == MsgTypes::Heartbeat ||
                   msgType == MsgTypes::TestRequest ||
                   msgType == MsgTypes::ResendRequest ||
                   msgType == MsgTypes::Reject ||
                   msgType == MsgTypes::SequenceReset ||
                   msgType == MsgTypes::Logout ||
                   msgType == MsgTypes::Logon;
        }

        std::string calculateChecksum(const std::string &message)
        {
            uint8_t checksum = 0;
            for (char c : message)
            {
                checksum += static_cast<uint8_t>(c);
            }

            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(3) << static_cast<int>(checksum % 256);
            return oss.str();
        }

        bool verifyChecksum(const std::string &message)
        {
            size_t checksumPos = message.rfind("10=");
            if (checksumPos == std::string::npos)
                return false;

            std::string bodyMessage = message.substr(0, checksumPos);
            std::string expectedChecksum = calculateChecksum(bodyMessage);

            size_t checksumStart = checksumPos + 3;
            size_t checksumEnd = message.find(FIX_SOH, checksumStart);
            if (checksumEnd == std::string::npos)
                checksumEnd = message.length();

            std::string actualChecksum = message.substr(checksumStart, checksumEnd - checksumStart);

            return expectedChecksum == actualChecksum;
        }
    }

    // =================================================================
    // RAW POINTER FACTORY METHOD IMPLEMENTATIONS (high-performance)
    // =================================================================

    FixMessage *FixMessage::createLogon(fix_gateway::common::MessagePool<FixMessage> &pool,
                                        const std::string &senderID,
                                        const std::string &targetID,
                                        int heartBeatInterval,
                                        int encryptMethod)
    {
        FixMessage *msg = pool.allocate();
        if (!msg)
            return nullptr;

        msg->setField(FixFields::MsgType, MsgTypes::Logon);
        msg->setSenderCompID(senderID);
        msg->setTargetCompID(targetID);
        msg->setField(FixFields::HeartBtInt, heartBeatInterval);
        msg->setField(FixFields::EncryptMethod, encryptMethod);

        return msg;
    }

    FixMessage *FixMessage::createHeartbeat(fix_gateway::common::MessagePool<FixMessage> &pool,
                                            const std::string &senderID,
                                            const std::string &targetID)
    {
        FixMessage *msg = pool.allocate();
        if (!msg)
            return nullptr;

        msg->setField(FixFields::MsgType, MsgTypes::Heartbeat);
        msg->setSenderCompID(senderID);
        msg->setTargetCompID(targetID);

        return msg;
    }

    FixMessage *FixMessage::createNewOrderSingle(fix_gateway::common::MessagePool<FixMessage> &pool,
                                                 const std::string &clOrdID,
                                                 const std::string &symbol,
                                                 const std::string &side,
                                                 const std::string &orderQty,
                                                 const std::string &price,
                                                 const std::string &orderType,
                                                 const std::string &timeInForce)
    {
        FixMessage *msg = pool.allocate();
        if (!msg)
            return nullptr;

        msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
        msg->setField(FixFields::ClOrdID, clOrdID);
        msg->setField(FixFields::Symbol, symbol);
        msg->setField(FixFields::Side, side);
        msg->setField(FixFields::OrderQty, orderQty);
        msg->setField(FixFields::Price, price);
        msg->setField(FixFields::OrdType, orderType);
        msg->setField(FixFields::TimeInForce, timeInForce);

        return msg;
    }

    FixMessage *FixMessage::createOrderCancelRequest(fix_gateway::common::MessagePool<FixMessage> &pool,
                                                     const std::string &origClOrdID,
                                                     const std::string &clOrdID,
                                                     const std::string &symbol,
                                                     const std::string &side)
    {
        FixMessage *msg = pool.allocate();
        if (!msg)
            return nullptr;

        msg->setField(FixFields::MsgType, MsgTypes::OrderCancelRequest);
        msg->setField(FixFields::OrigClOrdID, origClOrdID);
        msg->setField(FixFields::ClOrdID, clOrdID);
        msg->setField(FixFields::Symbol, symbol);
        msg->setField(FixFields::Side, side);

        return msg;
    }

    // Helper methods for raw pointer factories
    void FixMessage::setCommonSessionFields(FixMessage *msg, const std::string &senderID, const std::string &targetID)
    {
        if (msg)
        {
            msg->setSenderCompID(senderID);
            msg->setTargetCompID(targetID);
            msg->setSendingTime();
        }
    }

    void FixMessage::initializeSessionFields(const std::string &senderID, const std::string &targetID, int seqNum)
    {
        setSenderCompID(senderID);
        setTargetCompID(targetID);
        setMsgSeqNum(seqNum);
        setSendingTime();
    }

    void FixMessage::initializeAsNewOrderSingle(const std::string &clOrdID, const std::string &symbol,
                                                const std::string &side, const std::string &orderQty,
                                                const std::string &price, const std::string &orderType,
                                                const std::string &timeInForce)
    {
        setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
        setField(FixFields::ClOrdID, clOrdID);
        setField(FixFields::Symbol, symbol);
        setField(FixFields::Side, side);
        setField(FixFields::OrderQty, orderQty);
        setField(FixFields::Price, price);
        setField(FixFields::OrdType, orderType);
        setField(FixFields::TimeInForce, timeInForce);
    }

    void FixMessage::initializeAsOrderCancel(const std::string &origClOrdID, const std::string &clOrdID,
                                             const std::string &symbol, const std::string &side)
    {
        setField(FixFields::MsgType, MsgTypes::OrderCancelRequest);
        setField(FixFields::OrigClOrdID, origClOrdID);
        setField(FixFields::ClOrdID, clOrdID);
        setField(FixFields::Symbol, symbol);
        setField(FixFields::Side, side);
    }

    // FastFixPatterns implementations
    namespace FastFixPatterns
    {
        FixMessage *createFastOrder(fix_gateway::common::MessagePool<FixMessage> &pool,
                                    const std::string &clOrdID,
                                    const std::string &symbol,
                                    const std::string &side,
                                    const std::string &qty,
                                    const std::string &price)
        {
            // Step 1: Pool allocation (~100ns)
            FixMessage *msg = pool.allocate();
            if (!msg)
                return nullptr;

            // Step 2: Direct field setting (minimal overhead)
            msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
            msg->setField(FixFields::ClOrdID, clOrdID);
            msg->setField(FixFields::Symbol, symbol);
            msg->setField(FixFields::Side, side);
            msg->setField(FixFields::OrderQty, qty);
            msg->setField(FixFields::Price, price);
            msg->setField(FixFields::OrdType, "2");     // Limit order
            msg->setField(FixFields::TimeInForce, "0"); // Day order

            return msg; // Total: ~200-300ns vs ~8000ns for shared_ptr
        }

        FixMessage *createFastCancel(fix_gateway::common::MessagePool<FixMessage> &pool,
                                     const std::string &origClOrdID,
                                     const std::string &clOrdID,
                                     const std::string &symbol)
        {
            FixMessage *msg = pool.allocate();
            if (!msg)
                return nullptr;

            msg->setField(FixFields::MsgType, MsgTypes::OrderCancelRequest);
            msg->setField(FixFields::OrigClOrdID, origClOrdID);
            msg->setField(FixFields::ClOrdID, clOrdID);
            msg->setField(FixFields::Symbol, symbol);

            return msg;
        }
    }

} // namespace fix_gateway::protocol