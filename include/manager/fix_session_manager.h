#pragma once

#include "manager/inbound_message_manager.h"
#include "protocol/fix_message.h"
#include "common/message_pool.h"

#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <thread>

namespace fix_gateway::manager
{
    /**
     * @brief Handles FIX session management messages
     *
     * Processes session-level messages like LOGON, LOGOUT, HEARTBEAT, TEST_REQUEST.
     * Manages session state, sequence numbers, and heartbeat scheduling.
     * 
     * Routes processed messages to outbound priority queues:
     * - CRITICAL: Logon, Logout (session-critical)
     * - HIGH: Heartbeat responses, TestRequest responses (time-sensitive)
     * - MEDIUM: Sequence reset, administrative responses
     */
    class FixSessionManager : public InboundMessageManager
    {
    public:
        enum class SessionState
        {
            DISCONNECTED,
            CONNECTING,
            LOGON_SENT,
            LOGGED_ON,
            LOGOUT_SENT,
            DISCONNECTING
        };

        struct SessionConfig
        {
            std::string sender_comp_id;
            std::string target_comp_id;
            int heartbeat_interval = 30; // seconds
            bool reset_sequence_numbers = false;
            int logon_timeout_seconds = 30;
            bool validate_sequence_numbers = true;
        };

        struct SessionStats
        {
            uint64_t heartbeats_sent = 0;
            uint64_t heartbeats_received = 0;
            uint64_t test_requests_sent = 0;
            uint64_t test_requests_received = 0;
            uint64_t logons_sent = 0;
            uint64_t logouts_sent = 0;
            uint64_t sequence_resets_sent = 0;
            uint64_t rejects_sent = 0;

            std::chrono::steady_clock::time_point session_start_time;
            std::chrono::steady_clock::time_point last_heartbeat_time;
            SessionState current_state = SessionState::DISCONNECTED;
        };

    public:
        explicit FixSessionManager(const SessionConfig& config);
        ~FixSessionManager() override;

        // Session lifecycle
        void start() override;
        void stop() override;

        // Session management
        bool initiateLogon();
        bool initiateLogout(const std::string& reason = "");
        SessionState getSessionState() const { return session_state_.load(); }

        // Configuration
        void updateHeartbeatInterval(int seconds);
        void setSequenceNumbers(int incoming_seq, int outgoing_seq);
        void setMessagePool(std::shared_ptr<fix_gateway::common::MessagePool<FixMessage>> message_pool);

        // Session stats
        SessionStats getSessionStats() const { return session_stats_; }

        // Sequence number management
        int getNextOutgoingSeqNum() { return ++outgoing_seq_num_; }
        int getExpectedIncomingSeqNum() const { return expected_incoming_seq_num_; }

    protected:
        // Implementation of abstract methods from InboundMessageManager
        bool handleMessage(FixMessage* message) override;
        bool isMessageSupported(const FixMessage* message) const override;
        std::vector<FixMsgType> getHandledMessageTypes() const override;

    private:
        // Session message handlers
        bool handleLogon(FixMessage* message);
        bool handleLogout(FixMessage* message);
        bool handleHeartbeat(FixMessage* message);
        bool handleTestRequest(FixMessage* message);
        bool handleResendRequest(FixMessage* message);
        bool handleSequenceReset(FixMessage* message);
        bool handleReject(FixMessage* message);

        // Session response generators - create and route to outbound queues
        bool sendLogon();
        bool sendLogout(const std::string& reason);
        bool sendHeartbeat(const std::string& test_req_id = "");
        bool sendTestRequest();
        bool sendReject(int ref_seq_num, const std::string& reason);
        bool sendSequenceReset(int new_seq_num, bool gap_fill = false);

        // Sequence number validation
        bool validateSequenceNumber(const FixMessage* message);
        void handleSequenceNumberGap(int expected, int received);

        // Heartbeat management
        void startHeartbeatTimer();
        void stopHeartbeatTimer();
        void heartbeatTimerFunction();
        bool shouldSendHeartbeat() const;
        bool shouldSendTestRequest() const;

        // Session validation
        bool validateSessionMessage(const FixMessage* message) const;
        bool isValidSenderCompId(const std::string& sender_comp_id) const;
        bool isValidTargetCompId(const std::string& target_comp_id) const;

        // Utility methods
        void updateSessionState(SessionState new_state);
        std::string createTestRequestId();
        
        // Message creation helpers
        FixMessage* createLogonMessage();
        FixMessage* createLogoutMessage(const std::string& reason);
        FixMessage* createHeartbeatMessage(const std::string& test_req_id = "");
        FixMessage* createTestRequestMessage();
        FixMessage* createRejectMessage(int ref_seq_num, const std::string& reason);

    private:
        // Session configuration
        SessionConfig config_;
        
        // Message pool for response message creation
        std::shared_ptr<fix_gateway::common::MessagePool<FixMessage>> message_pool_;

        // Session state
        std::atomic<SessionState> session_state_{SessionState::DISCONNECTED};
        std::atomic<int> outgoing_seq_num_{1};
        std::atomic<int> expected_incoming_seq_num_{1};

        // Heartbeat management
        std::thread heartbeat_thread_;
        std::atomic<bool> heartbeat_timer_running_{false};
        std::chrono::steady_clock::time_point last_heartbeat_sent_;
        std::chrono::steady_clock::time_point last_message_received_;

        // Test request management
        std::string pending_test_request_id_;
        std::chrono::steady_clock::time_point test_request_sent_time_;

        // Statistics
        mutable SessionStats session_stats_;

        // Test request ID generation
        std::atomic<uint64_t> test_req_id_counter_{1};
    };

} // namespace fix_gateway::manager