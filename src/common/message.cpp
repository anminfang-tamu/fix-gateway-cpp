#include "common/message.h"
#include <sstream>
#include <atomic>

namespace fix_gateway::common
{
    // Simple constructor
    Message::Message(
        const std::string &message_id,
        const std::string &payload,
        Priority priority,
        MessageType message_type,
        const std::string &session_id,
        const std::string &destination)
        : message_id_(message_id),
          sequence_number_(generateSequenceNumber()),
          payload_(payload),
          payload_size_(payload.size()),
          priority_(priority),
          message_type_(message_type),
          session_id_(session_id),
          destination_(destination),
          queue_entry_time_ns_(0),
          send_time_ns_(0),
          deadline_time_ns_(0),
          user_context_(nullptr),
          state_(MessageState::PENDING),
          retry_count_(0),
          error_code_(0),
          error_message_ptr_(nullptr)
    {
        initializeTimestamps();
    }

    // Detailed constructor
    Message::Message(
        const std::string &message_id,
        const std::string &sequence_number,
        const std::string &payload,
        Priority priority,
        MessageType message_type,
        const std::string &session_id,
        const std::string &destination,
        const std::chrono::steady_clock::time_point &deadline)
        : message_id_(message_id),
          sequence_number_(sequence_number),
          payload_(payload),
          payload_size_(payload.size()),
          priority_(priority),
          message_type_(message_type),
          session_id_(session_id),
          destination_(destination),
          queue_entry_time_ns_(0),
          send_time_ns_(0),
          deadline_time_ns_(deadline.time_since_epoch().count() ? timePointToNanos(deadline) : 0),
          user_context_(nullptr),
          state_(MessageState::PENDING),
          retry_count_(0),
          error_code_(0),
          error_message_ptr_(nullptr)
    {
        initializeTimestamps();
    }

    // Copy constructor
    Message::Message(const Message &other)
        : queue_entry_time_ns_(0),
          send_time_ns_(0),
          deadline_time_ns_(0),
          user_context_(nullptr),
          state_(MessageState::PENDING),
          retry_count_(0),
          error_code_(0),
          error_message_ptr_(nullptr)
    {
        copyFrom(other);
    }

    // Move constructor
    Message::Message(Message &&other) noexcept
        : queue_entry_time_ns_(0),
          send_time_ns_(0),
          deadline_time_ns_(0),
          user_context_(nullptr),
          state_(MessageState::PENDING),
          retry_count_(0),
          error_code_(0),
          error_message_ptr_(nullptr)
    {
        moveFrom(std::move(other));
    }

    // Copy assignment operator
    Message &Message::operator=(const Message &other)
    {
        if (this != &other)
        {
            copyFrom(other);
        }
        return *this;
    }

    // Move assignment operator
    Message &Message::operator=(Message &&other) noexcept
    {
        if (this != &other)
        {
            moveFrom(std::move(other));
        }
        return *this;
    }

    // Destructor
    Message::~Message()
    {
        // Clean up atomic error message pointer
        std::string *error_msg = error_message_ptr_.load(std::memory_order_relaxed);
        if (error_msg)
        {
            delete error_msg;
        }
    }

    // Factory methods
    MessagePtr Message::create(
        const std::string &message_id,
        const std::string &payload,
        Priority priority, MessageType message_type,
        const std::string &session_id,
        const std::string &destination)
    {
        return std::make_shared<Message>(message_id, payload, priority, message_type, session_id, destination);
    }

    MessagePtr Message::create(
        const std::string &message_id,
        const std::string &sequence_number,
        const std::string &payload,
        Priority priority, MessageType message_type,
        const std::string &session_id,
        const std::string &destination,
        const std::chrono::steady_clock::time_point &deadline)
    {
        return std::make_shared<Message>(message_id, sequence_number, payload, priority, message_type, session_id, destination, deadline);
    }

    // Core data accessors
    const std::string &Message::getMessageId() const
    {
        return message_id_;
    }

    const std::string &Message::getSequenceNumber() const
    {
        return sequence_number_;
    }

    const std::string &Message::getPayload() const
    {
        return payload_;
    }

    uint64_t Message::getPayloadSize() const
    {
        return payload_size_;
    }

    Priority Message::getPriority() const
    {
        return priority_;
    }

    MessageType Message::getMessageType() const
    {
        return message_type_;
    }

    const std::string &Message::getSessionId() const
    {
        return session_id_;
    }

    const std::string &Message::getDestination() const
    {
        return destination_;
    }

    // Timing accessors
    std::chrono::steady_clock::time_point Message::getCreationTime() const
    {
        return creation_time_;
    }

    std::chrono::steady_clock::time_point Message::getQueueEntryTime() const
    {
        uint64_t nanos = queue_entry_time_ns_.load(std::memory_order_acquire);
        return nanos ? nanosToTimePoint(nanos) : std::chrono::steady_clock::time_point{};
    }

    std::chrono::steady_clock::time_point Message::getSendTime() const
    {
        uint64_t nanos = send_time_ns_.load(std::memory_order_acquire);
        return nanos ? nanosToTimePoint(nanos) : std::chrono::steady_clock::time_point{};
    }

    std::chrono::steady_clock::time_point Message::getDeadlineTime() const
    {
        uint64_t nanos = deadline_time_ns_.load(std::memory_order_acquire);
        return nanos ? nanosToTimePoint(nanos) : std::chrono::steady_clock::time_point{};
    }

    // Timing utilities
    std::chrono::nanoseconds Message::getQueueLatency() const
    {
        uint64_t queue_nanos = queue_entry_time_ns_.load(std::memory_order_acquire);
        if (queue_nanos == 0)
            return std::chrono::nanoseconds(0);

        uint64_t creation_nanos = timePointToNanos(creation_time_);
        return std::chrono::nanoseconds(queue_nanos - creation_nanos);
    }

    std::chrono::nanoseconds Message::getSendLatency() const
    {
        uint64_t send_nanos = send_time_ns_.load(std::memory_order_acquire);
        uint64_t queue_nanos = queue_entry_time_ns_.load(std::memory_order_acquire);

        if (send_nanos == 0 || queue_nanos == 0)
            return std::chrono::nanoseconds(0);

        return std::chrono::nanoseconds(send_nanos - queue_nanos);
    }

    std::chrono::nanoseconds Message::getTotalLatency() const
    {
        uint64_t send_nanos = send_time_ns_.load(std::memory_order_acquire);
        if (send_nanos == 0)
            return std::chrono::nanoseconds(0);

        uint64_t creation_nanos = timePointToNanos(creation_time_);
        return std::chrono::nanoseconds(send_nanos - creation_nanos);
    }

    bool Message::isExpired() const
    {
        uint64_t deadline_nanos = deadline_time_ns_.load(std::memory_order_acquire);
        if (deadline_nanos == 0)
            return false;

        auto now = std::chrono::steady_clock::now();
        return timePointToNanos(now) > deadline_nanos;
    }

    std::chrono::nanoseconds Message::getTimeToDeadline() const
    {
        uint64_t deadline_nanos = deadline_time_ns_.load(std::memory_order_acquire);
        if (deadline_nanos == 0)
            return std::chrono::nanoseconds::max();

        uint64_t now_nanos = timePointToNanos(std::chrono::steady_clock::now());
        if (now_nanos > deadline_nanos)
            return std::chrono::nanoseconds(0);

        return std::chrono::nanoseconds(deadline_nanos - now_nanos);
    }

    // State management
    MessageState Message::getState() const
    {
        return state_.load(std::memory_order_relaxed);
    }

    void Message::setState(MessageState state)
    {
        state_.store(state, std::memory_order_relaxed);
    }

    bool Message::isPending() const
    {
        return getState() == MessageState::PENDING;
    }

    bool Message::isSending() const
    {
        return getState() == MessageState::SENDING;
    }

    bool Message::isSent() const
    {
        return getState() == MessageState::SENT;
    }

    bool Message::isFailed() const
    {
        return getState() == MessageState::FAILED;
    }

    bool Message::isExpiredState() const
    {
        return getState() == MessageState::EXPIRED;
    }

    // Error handling
    int Message::getRetryCount() const
    {
        return retry_count_.load(std::memory_order_relaxed);
    }

    int Message::getErrorCode() const
    {
        return error_code_.load(std::memory_order_relaxed);
    }

    std::string Message::getErrorMessage() const
    {
        return getErrorMessageAtomic();
    }

    void Message::incrementRetryCount()
    {
        retry_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void Message::setError(int error_code, const std::string &error_message)
    {
        error_code_.store(error_code, std::memory_order_relaxed);
        setErrorMessageAtomic(error_message);
    }

    void Message::clearError()
    {
        error_code_.store(0, std::memory_order_relaxed);
        setErrorMessageAtomic("");
    }

    // Timing setters (lock-free atomic operations)
    void Message::setQueueEntryTime(const std::chrono::steady_clock::time_point &time)
    {
        uint64_t nanos = timePointToNanos(time);
        queue_entry_time_ns_.store(nanos, std::memory_order_release);
    }

    void Message::setSendTime(const std::chrono::steady_clock::time_point &time)
    {
        uint64_t nanos = timePointToNanos(time);
        send_time_ns_.store(nanos, std::memory_order_release);
    }

    void Message::setDeadlineTime(const std::chrono::steady_clock::time_point &time)
    {
        uint64_t nanos = timePointToNanos(time);
        deadline_time_ns_.store(nanos, std::memory_order_release);
    }

    // Callback management
    void Message::setCompletionCallback(CompletionCallback callback)
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        completion_callback_ = callback;
    }

    void Message::setErrorCallback(ErrorCallback callback)
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        error_callback_ = callback;
    }

    void Message::setUserCallback(UserCallback callback, void *user_context)
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        user_callback_ = callback;
        user_context_ = user_context;
    }

    // Callback execution
    void Message::executeCompletionCallback() const
    {
        CompletionCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = completion_callback_;
        }
        if (callback)
        {
            callback(*this);
        }
    }

    void Message::executeErrorCallback(int error_code, const std::string &error_message) const
    {
        ErrorCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = error_callback_;
        }
        if (callback)
        {
            callback(*this, error_code, error_message);
        }
    }

    void Message::executeUserCallback() const
    {
        UserCallback callback;
        void *context;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = user_callback_;
            context = user_context_;
        }
        if (callback)
        {
            callback(*this, context);
        }
    }

    // Utility methods
    std::string Message::toString() const
    {
        std::ostringstream oss;
        oss << "Message{"
            << "id=" << message_id_
            << ", seq=" << sequence_number_
            << ", type=" << getTypeString()
            << ", state=" << getStateString()
            << ", priority=" << getPriorityString()
            << ", payload_size=" << payload_size_
            << ", created=" << creation_time_.time_since_epoch().count()
            << ", queue_latency=" << getQueueLatency().count() << "ns"
            << ", send_latency=" << getSendLatency().count() << "ns"
            << ", total_latency=" << getTotalLatency().count() << "ns"
            << ", retry_count=" << getRetryCount()
            << "}";
        return oss.str();
    }

    std::string Message::getStateString() const
    {
        return messageStateToString(getState());
    }

    std::string Message::getTypeString() const
    {
        return messageTypeToString(getMessageType());
    }

    std::string Message::getPriorityString() const
    {
        return priorityToString(getPriority());
    }

    // Comparison operators
    bool Message::operator<(const Message &other) const
    {
        // For priority queue: higher priority value = higher priority
        // Return true if this message has LOWER priority than other
        return static_cast<int>(priority_) < static_cast<int>(other.priority_);
    }

    bool Message::operator>(const Message &other) const
    {
        return static_cast<int>(priority_) > static_cast<int>(other.priority_);
    }

    bool Message::operator==(const Message &other) const
    {
        return message_id_ == other.message_id_ &&
               sequence_number_ == other.sequence_number_;
    }

    // Thread safety (simplified for lock-free operations)
    void Message::lock() const
    {
        callback_mutex_.lock();
    }

    void Message::unlock() const
    {
        callback_mutex_.unlock();
    }

    bool Message::tryLock() const
    {
        return callback_mutex_.try_lock();
    }

    // Helper methods
    void Message::initializeTimestamps()
    {
        creation_time_ = std::chrono::steady_clock::now();
        queue_entry_time_ns_.store(0, std::memory_order_relaxed);
        send_time_ns_.store(0, std::memory_order_relaxed);
        if (deadline_time_ns_.load(std::memory_order_relaxed) == 0)
        {
            deadline_time_ns_.store(0, std::memory_order_relaxed);
        }
    }

    void Message::copyFrom(const Message &other)
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        std::lock_guard<std::mutex> other_lock(other.callback_mutex_);

        message_id_ = other.message_id_;
        sequence_number_ = other.sequence_number_;
        payload_ = other.payload_;
        payload_size_ = other.payload_size_;
        priority_ = other.priority_;
        message_type_ = other.message_type_;
        session_id_ = other.session_id_;
        destination_ = other.destination_;
        creation_time_ = other.creation_time_;
        queue_entry_time_ns_.store(other.queue_entry_time_ns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        send_time_ns_.store(other.send_time_ns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        deadline_time_ns_.store(other.deadline_time_ns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        completion_callback_ = other.completion_callback_;
        error_callback_ = other.error_callback_;
        user_callback_ = other.user_callback_;
        user_context_ = other.user_context_;
        state_.store(other.state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        retry_count_.store(other.retry_count_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        error_code_.store(other.error_code_.load(std::memory_order_relaxed), std::memory_order_relaxed);

        // Copy error message atomically
        std::string error_msg = other.getErrorMessageAtomic();
        setErrorMessageAtomic(error_msg);
    }

    void Message::moveFrom(Message &&other) noexcept
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        std::lock_guard<std::mutex> other_lock(other.callback_mutex_);

        message_id_ = std::move(other.message_id_);
        sequence_number_ = std::move(other.sequence_number_);
        payload_ = std::move(other.payload_);
        payload_size_ = other.payload_size_;
        priority_ = other.priority_;
        message_type_ = other.message_type_;
        session_id_ = std::move(other.session_id_);
        destination_ = std::move(other.destination_);
        creation_time_ = other.creation_time_;
        queue_entry_time_ns_.store(other.queue_entry_time_ns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        send_time_ns_.store(other.send_time_ns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        deadline_time_ns_.store(other.deadline_time_ns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        completion_callback_ = std::move(other.completion_callback_);
        error_callback_ = std::move(other.error_callback_);
        user_callback_ = std::move(other.user_callback_);
        user_context_ = other.user_context_;
        state_.store(other.state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        retry_count_.store(other.retry_count_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        error_code_.store(other.error_code_.load(std::memory_order_relaxed), std::memory_order_relaxed);

        // Move error message atomically
        std::string error_msg = other.getErrorMessageAtomic();
        setErrorMessageAtomic(error_msg);
        other.setErrorMessageAtomic(""); // Clear the source
    }

    // Lock-free timestamp conversion helpers
    uint64_t Message::timePointToNanos(const std::chrono::steady_clock::time_point &tp)
    {
        return static_cast<uint64_t>(tp.time_since_epoch().count());
    }

    std::chrono::steady_clock::time_point Message::nanosToTimePoint(uint64_t nanos)
    {
        return std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(nanos));
    }

    // Error message management (lock-free)
    void Message::setErrorMessageAtomic(const std::string &error_message)
    {
        std::string *new_msg = error_message.empty() ? nullptr : new std::string(error_message);
        std::string *old_msg = error_message_ptr_.exchange(new_msg, std::memory_order_acq_rel);
        if (old_msg)
        {
            delete old_msg;
        }
    }

    std::string Message::getErrorMessageAtomic() const
    {
        std::string *msg_ptr = error_message_ptr_.load(std::memory_order_acquire);
        return msg_ptr ? *msg_ptr : std::string();
    }

    std::string Message::generateSequenceNumber()
    {
        static std::atomic<uint64_t> sequence_counter{0};
        return std::to_string(sequence_counter.fetch_add(1, std::memory_order_relaxed));
    }

    // Utility functions - String to enum conversions using if-else (C++ doesn't support switch on strings)
    MessageType stringToMessageType(const std::string &type_str)
    {
        if (type_str == "ORDER")
            return MessageType::ORDER;
        if (type_str == "CANCEL")
            return MessageType::CANCEL;
        if (type_str == "FILL")
            return MessageType::FILL;
        if (type_str == "HEARTBEAT")
            return MessageType::HEARTBEAT;
        if (type_str == "LOGON")
            return MessageType::LOGON;
        if (type_str == "LOGOUT")
            return MessageType::LOGOUT;
        if (type_str == "REJECT")
            return MessageType::REJECT;
        if (type_str == "ACCEPT")
            return MessageType::ACCEPT;
        if (type_str == "ERROR")
            return MessageType::ERROR;
        return MessageType::UNKNOWN;
    }

    MessageState stringToMessageState(const std::string &state_str)
    {
        if (state_str == "PENDING")
            return MessageState::PENDING;
        if (state_str == "SENDING")
            return MessageState::SENDING;
        if (state_str == "SENT")
            return MessageState::SENT;
        if (state_str == "FAILED")
            return MessageState::FAILED;
        if (state_str == "EXPIRED")
            return MessageState::EXPIRED;
        return MessageState::PENDING; // Default to PENDING
    }

    Priority stringToPriority(const std::string &priority_str)
    {
        if (priority_str == "LOW")
            return Priority::LOW;
        if (priority_str == "MEDIUM")
            return Priority::MEDIUM;
        if (priority_str == "HIGH")
            return Priority::HIGH;
        if (priority_str == "CRITICAL")
            return Priority::CRITICAL;
        return Priority::LOW; // Default to LOW
    }

    // Enum to string conversions
    std::string messageTypeToString(MessageType type)
    {
        switch (type)
        {
        case MessageType::ORDER:
            return "ORDER";
        case MessageType::CANCEL:
            return "CANCEL";
        case MessageType::FILL:
            return "FILL";
        case MessageType::HEARTBEAT:
            return "HEARTBEAT";
        case MessageType::LOGON:
            return "LOGON";
        case MessageType::LOGOUT:
            return "LOGOUT";
        case MessageType::REJECT:
            return "REJECT";
        case MessageType::ACCEPT:
            return "ACCEPT";
        case MessageType::ERROR:
            return "ERROR";
        case MessageType::UNKNOWN:
        default:
            return "UNKNOWN";
        }
    }

    std::string messageStateToString(MessageState state)
    {
        switch (state)
        {
        case MessageState::PENDING:
            return "PENDING";
        case MessageState::SENDING:
            return "SENDING";
        case MessageState::SENT:
            return "SENT";
        case MessageState::FAILED:
            return "FAILED";
        case MessageState::EXPIRED:
            return "EXPIRED";
        default:
            return "UNKNOWN";
        }
    }

    std::string priorityToString(Priority priority)
    {
        switch (priority)
        {
        case Priority::LOW:
            return "LOW";
        case Priority::MEDIUM:
            return "MEDIUM";
        case Priority::HIGH:
            return "HIGH";
        case Priority::CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
        }
    }

} // namespace fix_gateway::common