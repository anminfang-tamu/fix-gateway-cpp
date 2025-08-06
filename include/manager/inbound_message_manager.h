#pragma once

#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message.h"
#include "network/async_sender.h"
#include "priority_config.h"

#include <memory>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

namespace fix_gateway::manager
{
    using FixMessage = fix_gateway::protocol::FixMessage;
    using FixMsgType = fix_gateway::protocol::FixMsgType;
    using MessagePtr = fix_gateway::common::MessagePtr;
    using AsyncSender = fix_gateway::network::AsyncSender;

    /**
     * @brief Abstract base class for inbound message managers
     *
     * Provides common functionality for processing inbound FIX messages.
     * Child classes (FixSessionManager, BusinessLogicManager) implement
     * specific message processing logic for their domain.
     */
    class InboundMessageManager
    {
    public:
        struct ProcessingStats
        {
            uint64_t total_messages_processed = 0;
            uint64_t total_messages_sent = 0;
            uint64_t total_processing_errors = 0;
            double avg_processing_time_ns = 0.0;
            std::chrono::steady_clock::time_point last_message_time;

            // Per-message-type breakdown
            std::unordered_map<FixMsgType, uint64_t> message_type_counts;
            std::unordered_map<FixMsgType, double> message_type_avg_latency;
        };

    public:
        explicit InboundMessageManager(const std::string &manager_name);
        virtual ~InboundMessageManager() = default;

        // Lifecycle management
        virtual void start();
        virtual void stop();
        bool isRunning() const { return running_.load(); }

        // Core message processing (template method pattern)
        bool processMessage(FixMessage *message);

        // AsyncSender integration
        void setAsyncSender(std::shared_ptr<AsyncSender> sender);
        bool isConnected() const;

        // Monitoring and stats
        ProcessingStats getStats() const { return stats_; }
        const std::string &getManagerName() const { return manager_name_; }

        // Configuration
        void setProcessingTimeout(std::chrono::milliseconds timeout) { processing_timeout_ = timeout; }

    protected:
        // Abstract methods - must be implemented by child classes
        virtual bool canHandleMessage(const FixMessage *message) const = 0;
        virtual bool handleMessage(FixMessage *message) = 0;
        virtual std::vector<FixMsgType> getSupportedMessageTypes() const = 0;

        // Utility methods for child classes
        bool sendResponse(MessagePtr response_message, Priority priority = Priority::MEDIUM);
        bool sendFixMessage(const FixMessage &fix_message, Priority priority = Priority::MEDIUM);

        // Message validation helpers
        bool validateMessage(const FixMessage *message) const;
        bool isSessionMessage(FixMsgType msg_type) const;
        bool isTradingMessage(FixMsgType msg_type) const;

        // Performance monitoring helpers
        void recordProcessingStart();
        void recordProcessingEnd(FixMsgType msg_type, bool success);

        // Logging helpers
        void logInfo(const std::string &message) const;
        void logError(const std::string &message) const;
        void logWarning(const std::string &message) const;

    private:
        // Manager identity
        std::string manager_name_;

        // State management
        std::atomic<bool> running_;

        // AsyncSender for outbound messages
        std::shared_ptr<AsyncSender> async_sender_;

        // Performance monitoring
        mutable ProcessingStats stats_;
        std::chrono::steady_clock::time_point processing_start_time_;

        // Configuration
        std::chrono::milliseconds processing_timeout_{1000}; // 1 second default

        // Helper methods
        void updateStats(FixMsgType msg_type, std::chrono::nanoseconds processing_time, bool success);
        Priority getDefaultPriorityForMessage(FixMsgType msg_type) const;
    };

    /**
     * @brief Factory for creating inbound message managers
     */
    class InboundMessageManagerFactory
    {
    public:
        static std::unique_ptr<InboundMessageManager> createFixSessionManager();
        static std::unique_ptr<InboundMessageManager> createBusinessLogicManager();

        // Create both managers with shared AsyncSender
        static std::pair<std::unique_ptr<InboundMessageManager>, std::unique_ptr<InboundMessageManager>>
        createManagerPair(std::shared_ptr<AsyncSender> shared_sender);
    };

} // namespace fix_gateway::manager
