#pragma once

#include "priority_config.h"

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <iostream>
#include <sstream>
#include <functional>

namespace fix_gateway::common
{
    enum class MessageType
    {
        ORDER,
        CANCEL,
        FILL,
        HEARTBEAT,
        LOGON,
        LOGOUT,
        REJECT,
        ACCEPT,
        ERROR,
        UNKNOWN
    };

    enum class MessageState
    {
        PENDING,
        SENDING,
        SENT,
        FAILED,
        EXPIRED,
    };

    // Forward declaration for pointer alias
    class Message;
    using MessagePtr = Message *;

    class Message
    {
        // Phase 3 pipeline optimizations:
        // 1. Messages flow through single-threaded stages with explicit ownership hand-off
        // 2. Timing data stored as plain integers; queues provide the necessary synchronization
        // 3. Error strings managed without atomic indirection to avoid unnecessary heap churn
        // 4. Target latency remains <50ns per message operation for sub-10μs end-to-end budget

    public:
        // Type aliases for callbacks
        using CompletionCallback = std::function<void(const Message &)>;
        using ErrorCallback = std::function<void(const Message &, int error_code, const std::string &error_msg)>;
        using UserCallback = std::function<void(const Message &, void *user_context)>;

        // Simple constructor for basic messages
        Message(const std::string &message_id,
                const std::string &payload,
                Priority priority = Priority::LOW,
                MessageType message_type = MessageType::UNKNOWN,
                const std::string &session_id = "",
                const std::string &destination = "");

        // Detailed constructor with all parameters
        Message(const std::string &message_id,
                const std::string &sequence_number,
                const std::string &payload,
                Priority priority,
                MessageType message_type,
                const std::string &session_id,
                const std::string &destination,
                const std::chrono::steady_clock::time_point &deadline = std::chrono::steady_clock::time_point{});

        // Copy constructor
        Message(const Message &other);

        // Move constructor
        Message(Message &&other) noexcept;

        // Copy assignment operator
        Message &operator=(const Message &other);

        // Move assignment operator
        Message &operator=(Message &&other) noexcept;

        // Destructor
        ~Message();

        // Factory methods for raw pointer creation
        static MessagePtr create(const std::string &message_id,
                                 const std::string &payload,
                                 Priority priority = Priority::LOW,
                                 MessageType message_type = MessageType::UNKNOWN,
                                 const std::string &session_id = "",
                                 const std::string &destination = "");

        static MessagePtr create(const std::string &message_id,
                                 const std::string &sequence_number,
                                 const std::string &payload,
                                 Priority priority,
                                 MessageType message_type,
                                 const std::string &session_id,
                                 const std::string &destination,
                                 const std::chrono::steady_clock::time_point &deadline = std::chrono::steady_clock::time_point{});

        static void destroy(MessagePtr message);

        // Core data accessors
        const std::string &getMessageId() const;
        const std::string &getSequenceNumber() const;
        const std::string &getPayload() const;
        uint64_t getPayloadSize() const;

        // Priority & routing accessors
        Priority getPriority() const;
        MessageType getMessageType() const;
        const std::string &getSessionId() const;
        const std::string &getDestination() const;

        // Timing & performance accessors
        std::chrono::steady_clock::time_point getCreationTime() const;
        std::chrono::steady_clock::time_point getQueueEntryTime() const;
        std::chrono::steady_clock::time_point getSendTime() const;
        std::chrono::steady_clock::time_point getDeadlineTime() const;

        // Calculated timing utilities
        std::chrono::nanoseconds getQueueLatency() const;
        std::chrono::nanoseconds getSendLatency() const;
        std::chrono::nanoseconds getTotalLatency() const;
        bool isExpired() const;
        std::chrono::nanoseconds getTimeToDeadline() const;

        // State management
        MessageState getState() const;
        void setState(MessageState state);
        bool isPending() const;
        bool isSending() const;
        bool isSent() const;
        bool isFailed() const;
        bool isExpiredState() const;

        // Error handling
        int getRetryCount() const;
        int getErrorCode() const;
        std::string getErrorMessage() const; // Returns copy to avoid reference issues
        void incrementRetryCount();
        void setError(int error_code, const std::string &error_message);
        void clearError();

        // Timing setters
        void setQueueEntryTime(const std::chrono::steady_clock::time_point &time);
        void setSendTime(const std::chrono::steady_clock::time_point &time);
        void setDeadlineTime(const std::chrono::steady_clock::time_point &time);

        // Callback management (still mutex-protected for safety)
        void setCompletionCallback(CompletionCallback callback);
        void setErrorCallback(ErrorCallback callback);
        void setUserCallback(UserCallback callback, void *user_context = nullptr);

        // Callback execution (thread-safe)
        void executeCompletionCallback() const;
        void executeErrorCallback(int error_code, const std::string &error_message) const;
        void executeUserCallback() const;

        // Utility methods
        std::string toString() const;
        std::string getStateString() const;
        std::string getTypeString() const;
        std::string getPriorityString() const;

        // Comparison operators for priority queue
        // Priority ordering: CRITICAL=3 > HIGH=2 > MEDIUM=1 > LOW=0
        bool operator<(const Message &other) const;
        bool operator>(const Message &other) const;
        bool operator==(const Message &other) const;

        // Thread safety (callbacks only)
        void lock() const;
        void unlock() const;
        bool tryLock() const;

    private:
        // Core data
        std::string message_id_;
        std::string sequence_number_;
        std::string payload_;
        uint64_t payload_size_;

        // Priority & routing
        Priority priority_;
        MessageType message_type_;
        std::string session_id_;
        std::string destination_;

        // Timing & performance (single-thread pipeline semantics)
        std::chrono::steady_clock::time_point creation_time_;
        uint64_t queue_entry_time_ns_; // Nanoseconds since epoch
        uint64_t send_time_ns_;        // Nanoseconds since epoch
        uint64_t deadline_time_ns_;    // Nanoseconds since epoch

        // Completion handling (callbacks still mutex-protected)
        CompletionCallback completion_callback_;
        ErrorCallback error_callback_;
        UserCallback user_callback_;
        void *user_context_;

        // Message state
        MessageState state_;

        // Error handling
        int retry_count_;
        int error_code_;
        std::string error_message_;

        // Thread safety (reduced mutex usage)
        // Only used for callbacks and copy/move operations
        mutable std::mutex callback_mutex_;

        // Helper methods
        void initializeTimestamps();
        void copyFrom(const Message &other);
        void moveFrom(Message &&other) noexcept;
        static std::string generateSequenceNumber();

        // Timestamp conversion helpers
        static uint64_t timePointToNanos(const std::chrono::steady_clock::time_point &tp);
        static std::chrono::steady_clock::time_point nanosToTimePoint(uint64_t nanos);

        // Error message management
        void setErrorMessageInternal(const std::string &error_message);
        std::string getErrorMessageInternal() const;
    };

    // Comparator for priority queue (higher priority = higher number)
    // CRITICAL=3 > HIGH=2 > MEDIUM=1 > LOW=0
    struct MessagePriorityComparator
    {
        bool operator()(const MessagePtr &lhs, const MessagePtr &rhs) const
        {
            // For priority_queue, this should return true if lhs has LOWER priority than rhs
            // So CRITICAL messages (3) get processed before LOW messages (0)
            return static_cast<int>(lhs->getPriority()) < static_cast<int>(rhs->getPriority());
        }
    };

    // Utility functions
    std::string messageTypeToString(MessageType type);
    std::string messageStateToString(MessageState state);
    std::string priorityToString(Priority priority);
    MessageType stringToMessageType(const std::string &type_str);
    MessageState stringToMessageState(const std::string &state_str);
    Priority stringToPriority(const std::string &priority_str);

} // namespace fix_gateway::common
