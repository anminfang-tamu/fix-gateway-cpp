#pragma once

#include "fix_fields.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

// Forward declaration for template usage
namespace fix_gateway::common
{
    template <typename T>
    class MessagePool;
}

namespace fix_gateway::protocol
{
    // Forward declaration
    class FixMessage;
    using FixMessagePool = fix_gateway::common::MessagePool<FixMessage>;

    class FixMessage
    {
    public:
        using FieldMap = std::unordered_map<int, std::string>;
        using FieldIterator = FieldMap::const_iterator;

        // Construction
        FixMessage();
        FixMessage(const std::string &rawMessage);
        explicit FixMessage(const FieldMap &fields);

        // Copy and move semantics
        FixMessage(const FixMessage &other);
        FixMessage(FixMessage &&other) noexcept;
        FixMessage &operator=(const FixMessage &other);
        FixMessage &operator=(FixMessage &&other) noexcept;

        // Destructor
        ~FixMessage() = default;

        // Core field operations (optimized for trading performance)
        void setField(int tag, const std::string &value);
        void setField(int tag, int value);
        void setField(int tag, double value, int precision = 2);
        void setField(int tag, char value);
        void setField(int tag, std::string_view value);

        bool getField(int tag, std::string &value) const;
        bool getField(int tag, int &value) const;
        bool getField(int tag, double &value) const;
        bool getField(int tag, char &value) const;

        // Direct field access (fastest)
        const std::string *getFieldPtr(int tag) const;
        bool hasField(int tag) const;
        void removeField(int tag);

        // Common field accessors (trading-specific optimization)
        std::string getMsgType() const { return getFieldValue(FixFields::MsgType); }
        std::string getClOrdID() const { return getFieldValue(FixFields::ClOrdID); }
        std::string getSymbol() const { return getFieldValue(FixFields::Symbol); }
        std::string getSide() const { return getFieldValue(FixFields::Side); }
        std::string getOrderQty() const { return getFieldValue(FixFields::OrderQty); }
        std::string getPrice() const { return getFieldValue(FixFields::Price); }
        int getMsgSeqNum() const;

        // Ultra-fast message type classification (cached enum - Option 3 optimization)
        FixMsgType getMsgTypeEnum() const;

        // Session-level fields
        void setSenderCompID(const std::string &senderID);
        void setTargetCompID(const std::string &targetID);
        void setMsgSeqNum(int seqNum);
        void setSendingTime(); // Sets to current time
        void setSendingTime(const std::chrono::system_clock::time_point &time);

        // Message validation
        bool isValid() const;
        bool isAdminMessage() const;
        bool isApplicationMessage() const;
        std::vector<std::string> validate() const; // Returns list of validation errors

        // Serialization
        std::string toString() const;
        std::string toStringWithoutChecksum() const;
        size_t calculateBodyLength() const;
        std::string calculateChecksum() const;
        void updateLengthAndChecksum();

        // Message metadata
        size_t getFieldCount() const { return fields_.size(); }
        const FieldMap &getAllFields() const { return fields_; }
        std::chrono::steady_clock::time_point getCreationTime() const { return creationTime_; }
        std::chrono::steady_clock::time_point getLastModified() const { return lastModified_; }

        // Iterator support for field traversal
        FieldIterator begin() const { return fields_.begin(); }
        FieldIterator end() const { return fields_.end(); }

        // =================================================================
        // RAW POINTER FACTORY METHODS (high-performance)
        // =================================================================

        // Factory methods for maximum performance - use with MessagePool<FixMessage>
        // Raw pointer interface eliminates smart pointer overhead for critical trading paths

        // Session management (raw pointer versions)
        static FixMessage *createLogon(FixMessagePool &pool,
                                       const std::string &senderID, const std::string &targetID,
                                       int heartBeatInterval, int encryptMethod = 0);

        static FixMessage *createLogout(FixMessagePool &pool,
                                        const std::string &senderID, const std::string &targetID,
                                        const std::string &text = "");

        static FixMessage *createHeartbeat(FixMessagePool &pool,
                                           const std::string &senderID, const std::string &targetID);

        static FixMessage *createTestRequest(FixMessagePool &pool,
                                             const std::string &senderID, const std::string &targetID,
                                             const std::string &testReqID);

        // Trading message factory methods - CRITICAL PATH OPTIMIZATION
        static FixMessage *createNewOrderSingle(FixMessagePool &pool,
                                                const std::string &clOrdID, const std::string &symbol,
                                                const std::string &side, const std::string &orderQty,
                                                const std::string &price, const std::string &orderType,
                                                const std::string &timeInForce);

        static FixMessage *createOrderCancelRequest(FixMessagePool &pool,
                                                    const std::string &origClOrdID, const std::string &clOrdID,
                                                    const std::string &symbol, const std::string &side);

        static FixMessage *createExecutionReport(FixMessagePool &pool,
                                                 const std::string &orderID, const std::string &execID,
                                                 const std::string &execType, const std::string &ordStatus,
                                                 const std::string &symbol, const std::string &side,
                                                 const std::string &lastQty, const std::string &lastPx,
                                                 const std::string &leavesQty, const std::string &cumQty);

        // Initialize message with common session fields (call after pool allocation)
        void initializeSessionFields(const std::string &senderID, const std::string &targetID, int seqNum);

        // Initialize as trading message (call after pool allocation)
        void initializeAsNewOrderSingle(const std::string &clOrdID, const std::string &symbol,
                                        const std::string &side, const std::string &orderQty,
                                        const std::string &price, const std::string &orderType,
                                        const std::string &timeInForce);

        void initializeAsOrderCancel(const std::string &origClOrdID, const std::string &clOrdID,
                                     const std::string &symbol, const std::string &side);

        // Performance monitoring
        void markProcessingStart();
        void markProcessingEnd();
        std::chrono::nanoseconds getProcessingLatency() const;

        // Debug and logging
        std::string toFormattedString() const; // Pretty-printed with field names
        std::string getFieldsSummary() const;  // One-line summary of key fields

    private:
        // Core data
        FieldMap fields_;

        // Metadata
        std::chrono::steady_clock::time_point creationTime_;
        std::chrono::steady_clock::time_point lastModified_;
        std::chrono::steady_clock::time_point processingStart_;
        std::chrono::steady_clock::time_point processingEnd_;

        // Cached values for performance (mutable for lazy computation)
        mutable bool checksumValid_ = false;
        mutable bool lengthValid_ = false;
        mutable std::string cachedString_;
        mutable bool stringCacheValid_ = false;

        // Cached message type enum for ultra-fast classification (Option 3 optimization)
        mutable FixMsgType cachedMsgType_ = FixMsgType::UNKNOWN;
        mutable bool msgTypeCached_ = false;

        // Helper methods
        std::string getFieldValue(int tag) const;
        void setFieldInternal(int tag, const std::string &value);
        void invalidateCache();
        void touchModified();

        // Validation helpers
        bool hasRequiredSessionFields() const;
        bool hasValidFormat() const;
        bool isChecksumCorrect() const;

        // Parsing helper
        void parseFromString(const std::string &rawMessage);

        // Raw pointer factory helper (avoids code duplication)
        static void setCommonSessionFields(FixMessage *msg, const std::string &senderID, const std::string &targetID);
    };

    // Utility functions for FIX protocol
    namespace FixMessageUtils
    {
        // Message type checking
        bool isSessionMessage(const std::string &msgType);
        bool isOrderManagementMessage(const std::string &msgType);
        bool isMarketDataMessage(const std::string &msgType);

        // Field value validation
        bool isValidSide(const std::string &side);
        bool isValidOrderType(const std::string &orderType);
        bool isValidTimeInForce(const std::string &tif);
        bool isValidOrderStatus(const std::string &status);
        bool isValidExecType(const std::string &execType);

        // Time formatting for FIX
        std::string formatFixTime(const std::chrono::system_clock::time_point &time);
        std::chrono::system_clock::time_point parseFixTime(const std::string &fixTime);

        // Numeric validation
        bool isValidPrice(const std::string &price);
        bool isValidQuantity(const std::string &qty);

        // Field ordering for message serialization (FIX requires specific order)
        std::vector<int> getFieldOrder(const std::string &msgType);

        // Checksum calculation
        std::string calculateChecksum(const std::string &message);
        bool verifyChecksum(const std::string &message);

        // Message type descriptions (for logging/debugging)
        std::string getMessageTypeDescription(const std::string &msgType);
    }

    // =================================================================
    // HIGH-PERFORMANCE USAGE PATTERNS
    // =================================================================

    namespace FastFixPatterns
    {
        // Ultra-fast order creation pattern (for hot trading paths)
        FixMessage *createFastOrder(fix_gateway::common::MessagePool<FixMessage> &pool,
                                    const std::string &clOrdID,
                                    const std::string &symbol,
                                    const std::string &side,
                                    const std::string &qty,
                                    const std::string &price);

        // Fast cancel pattern
        FixMessage *createFastCancel(fix_gateway::common::MessagePool<FixMessage> &pool,
                                     const std::string &origClOrdID,
                                     const std::string &clOrdID,
                                     const std::string &symbol);
    }

} // namespace fix_gateway::protocol