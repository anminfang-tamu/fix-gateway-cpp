#pragma once

#include "fix_message.h"
#include "fix_fields.h"
#include <string>
#include <memory>
#include <chrono>
#include <sstream>

namespace fix_gateway::protocol
{
    class FixBuilder
    {
    public:
        // Builder configuration
        struct BuilderConfig
        {
            std::string senderCompID;
            std::string targetCompID;
            std::string beginString = FIX_VERSION_44;
            bool autoSequenceNumber = true;
            bool autoTimestamp = true;
            bool autoBodyLength = true;
            bool autoChecksum = true;
            bool enforceFieldOrder = true;
        };

        // Builder statistics for performance monitoring
        struct BuilderStats
        {
            uint64_t messagesBuildAttempts = 0;
            uint64_t messagesBuildSuccess = 0;
            uint64_t messagesBuildFailure = 0;
            uint64_t totalBuildTimeNanos = 0;
            uint64_t averageBuildTimeNanos = 0;
            uint64_t lastBuildTimeNanos = 0;

            void updateTiming(uint64_t buildTimeNanos);
            void reset();
        };

        // Constructor
        explicit FixBuilder(const BuilderConfig &config);
        FixBuilder(const std::string &senderCompID, const std::string &targetCompID);

        // Copy and move semantics
        FixBuilder(const FixBuilder &) = default;
        FixBuilder(FixBuilder &&) = default;
        FixBuilder &operator=(const FixBuilder &) = default;
        FixBuilder &operator=(FixBuilder &&) = default;

        // Configuration management
        void updateConfig(const BuilderConfig &config);
        const BuilderConfig &getConfig() const { return config_; }

        void setSenderCompID(const std::string &senderID);
        void setTargetCompID(const std::string &targetID);
        void setNextSeqNum(int seqNum) { nextSeqNum_ = seqNum; }
        int getNextSeqNum() const { return nextSeqNum_; }
        int getCurrentSeqNum() const { return nextSeqNum_ - 1; }

        // Core building methods
        std::string buildMessage(const FixMessage &message);
        std::string buildMessage(FixMessagePtr message);
        bool buildMessage(const FixMessage &message, std::string &output);

        // Session-level messages (administrative)
        std::string buildLogon(int heartBeatInterval, int encryptMethod = 0);
        std::string buildLogout(const std::string &text = "");
        std::string buildHeartbeat(const std::string &testReqID = "");
        std::string buildTestRequest(const std::string &testReqID);
        std::string buildResendRequest(int beginSeqNo, int endSeqNo = 0);
        std::string buildSequenceReset(int newSeqNo, bool gapFillFlag = false);
        std::string buildReject(int refSeqNum, const std::string &refMsgType,
                                const std::string &reason);

        // Trading messages (application)
        std::string buildNewOrderSingle(const std::string &clOrdID,
                                        const std::string &symbol,
                                        const std::string &side,
                                        const std::string &orderQty,
                                        const std::string &price,
                                        const std::string &orderType = OrderType::Limit,
                                        const std::string &timeInForce = TimeInForce::Day,
                                        const std::string &account = "");

        std::string buildOrderCancelRequest(const std::string &origClOrdID,
                                            const std::string &clOrdID,
                                            const std::string &symbol,
                                            const std::string &side,
                                            const std::string &orderQty = "");

        std::string buildOrderCancelReplaceRequest(const std::string &origClOrdID,
                                                   const std::string &clOrdID,
                                                   const std::string &symbol,
                                                   const std::string &side,
                                                   const std::string &orderQty,
                                                   const std::string &price,
                                                   const std::string &orderType = OrderType::Limit);

        std::string buildExecutionReport(const std::string &orderID,
                                         const std::string &execID,
                                         const std::string &execType,
                                         const std::string &ordStatus,
                                         const std::string &symbol,
                                         const std::string &side,
                                         const std::string &orderQty,
                                         const std::string &lastQty = "0",
                                         const std::string &lastPx = "0",
                                         const std::string &leavesQty = "0",
                                         const std::string &cumQty = "0",
                                         const std::string &avgPx = "0");

        // Market data messages
        std::string buildMarketDataRequest(const std::string &mdReqID,
                                           const std::string &subscriptionRequestType,
                                           int marketDepth,
                                           const std::vector<std::string> &symbols);

        // Batch building for high-throughput scenarios
        std::vector<std::string> buildMessages(const std::vector<FixMessagePtr> &messages);
        size_t buildMessages(const std::vector<FixMessagePtr> &messages,
                             std::vector<std::string> &output);

        // Advanced building with custom fields
        class MessageBuilder
        {
        public:
            MessageBuilder(FixBuilder &parent, const std::string &msgType);

            MessageBuilder &setField(int tag, const std::string &value);
            MessageBuilder &setField(int tag, int value);
            MessageBuilder &setField(int tag, double value, int precision = 2);
            MessageBuilder &setField(int tag, char value);

            // Common field shortcuts
            MessageBuilder &setClOrdID(const std::string &clOrdID);
            MessageBuilder &setSymbol(const std::string &symbol);
            MessageBuilder &setSide(const std::string &side);
            MessageBuilder &setOrderQty(const std::string &qty);
            MessageBuilder &setPrice(const std::string &price);
            MessageBuilder &setAccount(const std::string &account);
            MessageBuilder &setText(const std::string &text);

            std::string build();
            FixMessagePtr buildMessage();

        private:
            FixBuilder &parent_;
            FixMessage message_;
        };

        MessageBuilder createMessage(const std::string &msgType);

        // Statistics and monitoring
        const BuilderStats &getStats() const { return stats_; }
        void resetStats() { stats_.reset(); }

        // Validation
        bool validateMessage(const FixMessage &message) const;
        std::vector<std::string> getValidationErrors(const FixMessage &message) const;

    private:
        BuilderConfig config_;
        int nextSeqNum_ = 1;
        BuilderStats stats_;

        // Core building implementation
        std::string buildImpl(const FixMessage &message);
        void addStandardHeader(FixMessage &message, const std::string &msgType);
        void addStandardTrailer(FixMessage &message);
        void orderFields(FixMessage &message) const;

        // Field ordering helpers
        std::vector<int> getHeaderFieldOrder() const;
        std::vector<int> getTrailerFieldOrder() const;
        std::vector<int> getBodyFieldOrder(const std::string &msgType) const;

        // Utility methods
        std::string getCurrentTimestamp() const;
        size_t calculateBodyLength(const FixMessage &message) const;
        std::string calculateChecksum(const std::string &message) const;

        // Performance helpers
        void startTiming();
        void endTiming();
        std::chrono::steady_clock::time_point buildStartTime_;
    };

    // Standalone utility functions for FIX building
    namespace FixBuilderUtils
    {
        // Message templates for common trading scenarios
        FixMessagePtr createMarketOrder(const std::string &clOrdID,
                                        const std::string &symbol,
                                        const std::string &side,
                                        const std::string &orderQty);

        FixMessagePtr createLimitOrder(const std::string &clOrdID,
                                       const std::string &symbol,
                                       const std::string &side,
                                       const std::string &orderQty,
                                       const std::string &price);

        FixMessagePtr createIOCOrder(const std::string &clOrdID,
                                     const std::string &symbol,
                                     const std::string &side,
                                     const std::string &orderQty,
                                     const std::string &price);

        // Field formatting utilities
        std::string formatPrice(double price, int precision = 2);
        std::string formatQuantity(int quantity);
        std::string formatQuantity(double quantity, int precision = 0);
        std::string formatFixTimestamp(const std::chrono::system_clock::time_point &time);
        std::string formatFixTimestamp(); // Current time

        // Order ID generation
        std::string generateClOrdID(const std::string &prefix = "");
        std::string generateExecID(const std::string &prefix = "");

        // Validation utilities
        bool isValidClOrdID(const std::string &clOrdID);
        bool isValidSymbol(const std::string &symbol);

        // Message size estimation (for buffer pre-allocation)
        size_t estimateMessageSize(const std::string &msgType);
        size_t estimateMessageSize(const FixMessage &message);

        // Performance profiling
        struct BuildProfile
        {
            std::chrono::nanoseconds headerBuildTime{0};
            std::chrono::nanoseconds bodyBuildTime{0};
            std::chrono::nanoseconds trailerBuildTime{0};
            std::chrono::nanoseconds serializationTime{0};
            size_t fieldCount = 0;
            size_t messageLength = 0;
        };

        BuildProfile profileBuilding(const FixMessage &message);
    }

} // namespace fix_gateway::protocol