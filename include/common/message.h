#pragma once

#include "priority_config.h"

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
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

    // Forward declaration for shared_ptr typedef
    class Message;
    using MessagePtr = std::shared_ptr<Message>;

    class Message
    {
        // TODO: Phase 3 Lock-Free Migration Plan:
        // 1. Replace mutex with lock-free atomics for all timestamp operations
        // 2. Convert error_message_ to atomic or lock-free string
        // 3. Use memory ordering for cross-thread synchronization
        // 4. Target: <100ns per message operation for sub-10μs latency

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

        // Factory methods for shared_ptr creation
        // shared_ptr can be used to pass the message to other threads
        // like producer, consumer, callback, monitoring threads
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
        const std::string &getErrorMessage() const;
        void incrementRetryCount();
        void setError(int error_code, const std::string &error_message);
        void clearError();

        // Timing setters (thread-safe)
        void setQueueEntryTime(const std::chrono::steady_clock::time_point &time);
        void setSendTime(const std::chrono::steady_clock::time_point &time);
        void setDeadlineTime(const std::chrono::steady_clock::time_point &time);

        // Callback management
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

        // Thread safety
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

        // Timing & performance
        std::chrono::steady_clock::time_point creation_time_;
        std::chrono::steady_clock::time_point queue_entry_time_;
        std::chrono::steady_clock::time_point send_time_;
        std::chrono::steady_clock::time_point deadline_time_;

        // Completion handling
        CompletionCallback completion_callback_;
        ErrorCallback error_callback_;
        UserCallback user_callback_;
        void *user_context_;

        // Message state
        std::atomic<MessageState> state_;

        // Error handling
        std::atomic<int> retry_count_;
        std::atomic<int> error_code_;
        // TODO: Phase 3 - Make error_message_ atomic or lock-free for consistency
        std::string error_message_;

        // Thread safety
        // TODO: Phase 3 - Replace mutex with lock-free atomics for sub-10μs latency
        // Current mutex adds ~100ns overhead but ensures correctness for Phase 2
        mutable std::mutex mutex_;

        // Helper methods
        void initializeTimestamps();
        void copyFrom(const Message &other);
        void moveFrom(Message &&other) noexcept;
        static std::string generateSequenceNumber();
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