#pragma once

#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "utils/lockfree_queue.h"
#include "application/priority_queue_container.h"
#include "../../config/priority_config.h"

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
    using LockFreeQueue = fix_gateway::utils::LockFreeQueue<FixMessage*>;

    /**
     * @brief Abstract base class for inbound message managers
     *
     * Processes inbound FIX messages and routes them to outbound priority queues.
     * Follows the architecture: Inbound Queue → Processing → Outbound Priority Queues
     * 
     * CRITICAL DESIGN PRINCIPLE:
     * - Managers ONLY process messages and route to outbound queues
     * - AsyncSender threads independently handle TCP/network transmission
     * - NO direct network operations in message managers
     */
    class InboundMessageManager
    {
    public:
        struct ProcessingStats
        {
            uint64_t total_messages_processed = 0;
            uint64_t total_messages_routed = 0;
            uint64_t total_processing_errors = 0;
            uint64_t total_routing_errors = 0;
            double avg_processing_time_ns = 0.0;
            std::chrono::steady_clock::time_point last_message_time;

            // Per-message-type breakdown
            std::unordered_map<FixMsgType, uint64_t> message_type_counts;
            std::unordered_map<FixMsgType, double> message_type_avg_latency;
            
            // Per-priority routing counts
            uint64_t critical_routed = 0;
            uint64_t high_routed = 0;
            uint64_t medium_routed = 0;
            uint64_t low_routed = 0;
        };

    public:
        explicit InboundMessageManager(const std::string& manager_name);
        virtual ~InboundMessageManager() = default;

        // Lifecycle management
        virtual void start();
        virtual void stop();
        bool isRunning() const { return running_.load(); }

        // Queue integration - core of the architecture
        void setInboundQueue(std::shared_ptr<LockFreeQueue> inbound_queue);
        void setOutboundQueues(std::shared_ptr<PriorityQueueContainer> outbound_queues);

        // Main processing loop - polls inbound queue and processes messages
        void processMessages();
        
        // Single message processing (for testing and manual invocation)
        bool processMessage(FixMessage* message);
        
        // Public message handler for testing (delegates to protected handleMessage)
        bool handleMessagePublic(FixMessage* message) { return handleMessage(message); }

        // Public interface for testing and external access
        bool canHandleMessage(const FixMessage* message) const;
        std::vector<FixMsgType> getSupportedMessageTypes() const;

        // Monitoring and stats
        ProcessingStats getStats() const { return stats_; }
        const std::string& getManagerName() const { return manager_name_; }

        // Configuration
        void setProcessingTimeout(std::chrono::milliseconds timeout) { processing_timeout_ = timeout; }

    protected:
        // Abstract methods - must be implemented by child classes
        virtual bool handleMessage(FixMessage* message) = 0;
        virtual bool isMessageSupported(const FixMessage* message) const = 0;
        virtual std::vector<FixMsgType> getHandledMessageTypes() const = 0;

        // Message routing to outbound queues
        bool routeToOutbound(FixMessage* message, Priority priority);
        
        // Response message creation and routing
        bool sendResponse(FixMessage* response_message, Priority priority = Priority::MEDIUM);
        
        // Message validation helpers
        bool validateMessage(const FixMessage* message) const;
        bool isSessionMessage(FixMsgType msg_type) const;
        bool isTradingMessage(FixMsgType msg_type) const;

        // Priority determination based on message type
        Priority getMessagePriority(FixMsgType msg_type) const;

        // Performance monitoring helpers
        void recordProcessingStart();
        void recordProcessingEnd(FixMsgType msg_type, bool success, bool routed = false);

        // Logging helpers
        void logInfo(const std::string& message) const;
        void logError(const std::string& message) const;
        void logWarning(const std::string& message) const;
        void logDebug(const std::string& message) const;

    private:
        // Manager identity
        std::string manager_name_;

        // State management
        std::atomic<bool> running_;

        // Queue connections - core architecture
        std::shared_ptr<LockFreeQueue> inbound_queue_;
        std::shared_ptr<PriorityQueueContainer> outbound_queues_;

        // Performance monitoring
        mutable ProcessingStats stats_;
        std::chrono::steady_clock::time_point processing_start_time_;

        // Configuration
        std::chrono::milliseconds processing_timeout_{1000}; // 1 second default

        // Helper methods
        void updateStats(FixMsgType msg_type, std::chrono::nanoseconds processing_time, 
                        bool success, bool routed = false);
        
        // Message type classification
        bool isSessionMessageType(FixMsgType msg_type) const;
        bool isTradingMessageType(FixMsgType msg_type) const;
        bool isAdminMessageType(FixMsgType msg_type) const;
    };

    /**
     * @brief Simple factory for creating message managers
     * 
     * Creates managers with proper queue connections following the architecture
     */
    class MessageManagerFactory
    {
    public:
        // Create session manager with queue connections
        static std::unique_ptr<InboundMessageManager> createFixSessionManager(
            const std::string& manager_name = "FixSessionManager");
        
        // Create business logic manager with queue connections  
        static std::unique_ptr<InboundMessageManager> createBusinessLogicManager(
            const std::string& manager_name = "BusinessLogicManager");
    };

} // namespace fix_gateway::manager