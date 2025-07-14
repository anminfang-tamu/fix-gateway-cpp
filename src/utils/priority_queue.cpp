#include "utils/priority_queue.h"
#include "utils/logger.h"
#include "utils/performance_timer.h"
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace fix_gateway::utils
{
    // Constructor
    PriorityQueue::PriorityQueue(
        size_t max_size,
        OverflowPolicy overflow_policy,
        const std::string &queue_name)
        : max_size_(max_size),
          overflow_policy_(overflow_policy),
          queue_name_(queue_name),
          is_shutdown_(false),
          total_pushed_(0),
          total_popped_(0),
          total_dropped_(0),
          peak_size_(0),
          total_latency_ns_(0),
          latency_samples_(0)
    {
        LOG_INFO("PriorityQueue '" + queue_name_ + "' created with max_size=" +
                 std::to_string(max_size_) + ", policy=" + getOverflowPolicyString());
    }

    // Destructor
    PriorityQueue::~PriorityQueue()
    {
        shutdown();
        LOG_INFO("PriorityQueue '" + queue_name_ + "' destroyed. Final stats: " + toString());
    }

    // Core operations - Push with default timeout
    bool PriorityQueue::push(MessagePtr message)
    {
        return push(message, std::chrono::milliseconds::max());
    }

    // Core operations - Push with timeout
    bool PriorityQueue::push(MessagePtr message, std::chrono::milliseconds timeout)
    {
        if (is_shutdown_.load(std::memory_order_relaxed))
        {
            LOG_DEBUG("Push rejected: queue is shutdown");
            return false;
        }

        if (!message)
        {
            LOG_ERROR("Push rejected: null message");
            return false;
        }

        auto start_time = std::chrono::steady_clock::now();

        // Set queue entry timestamp
        message->setQueueEntryTime(start_time);

        std::unique_lock<std::mutex> lock(mutex_);

        // Handle different overflow policies
        if (queue_.size() >= max_size_)
        {
            switch (overflow_policy_)
            {
            case OverflowPolicy::DROP_OLDEST:
                if (!handleOverflow(message))
                    return false;
                break;

            case OverflowPolicy::DROP_NEWEST:
                total_dropped_.fetch_add(1, std::memory_order_relaxed);
                LOG_DEBUG("Message dropped due to queue overflow (DROP_NEWEST policy)");
                return false;

            case OverflowPolicy::BLOCK:
            {
                bool success = not_full_cv_.wait_for(lock, timeout,
                                                     [this]
                                                     { return queue_.size() < max_size_ || is_shutdown_.load(std::memory_order_relaxed); });
                if (!success || is_shutdown_.load(std::memory_order_relaxed))
                {
                    LOG_DEBUG("Push timed out or queue shutdown");
                    return false;
                }
            }
            break;

            case OverflowPolicy::REJECT:
                total_dropped_.fetch_add(1, std::memory_order_relaxed);
                LOG_DEBUG("Message rejected due to queue overflow (REJECT policy)");
                return false;
            }
        }

        // Perform the actual push
        queue_.push(message);
        total_pushed_.fetch_add(1, std::memory_order_relaxed);

        // Update peak size
        size_t current_size = queue_.size();
        size_t current_peak = peak_size_.load(std::memory_order_relaxed);
        while (current_size > current_peak &&
               !peak_size_.compare_exchange_weak(current_peak, current_size, std::memory_order_relaxed))
        {
            // Loop until we successfully update peak_size or current_size <= current_peak
        }

        // Record latency
        recordLatency(start_time);

        // Notify waiting consumers
        not_empty_cv_.notify_one();

        LOG_DEBUG("Message pushed to queue '" + queue_name_ + "', size=" + std::to_string(current_size));
        return true;
    }

    // Blocking pop - waits indefinitely
    bool PriorityQueue::pop(MessagePtr &message)
    {
        return pop(message, std::chrono::milliseconds::max());
    }

    // Blocking pop with timeout
    bool PriorityQueue::pop(MessagePtr &message, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // Wait for message or shutdown
        bool success = not_empty_cv_.wait_for(lock, timeout,
                                              [this]
                                              { return !queue_.empty() || is_shutdown_.load(std::memory_order_relaxed); });

        if (!success || (queue_.empty() && is_shutdown_.load(std::memory_order_relaxed)))
        {
            return false;
        }

        if (queue_.empty())
        {
            return false;
        }

        // Get the highest priority message
        message = queue_.top();
        queue_.pop();
        total_popped_.fetch_add(1, std::memory_order_relaxed);

        // Notify waiting producers
        not_full_cv_.notify_one();

        LOG_DEBUG("Message popped from queue '" + queue_name_ + "', size=" + std::to_string(queue_.size()));
        return true;
    }

    // Non-blocking pop
    bool PriorityQueue::tryPop(MessagePtr &message)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty())
        {
            return false;
        }

        message = queue_.top();
        queue_.pop();
        total_popped_.fetch_add(1, std::memory_order_relaxed);

        // Notify waiting producers
        not_full_cv_.notify_one();

        return true;
    }

    // Queue management
    void PriorityQueue::clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t cleared_count = queue_.size();

        // Clear the queue
        while (!queue_.empty())
        {
            queue_.pop();
        }

        // Notify all waiting threads
        not_full_cv_.notify_all();

        LOG_INFO("Queue '" + queue_name_ + "' cleared, removed " + std::to_string(cleared_count) + " messages");
    }

    void PriorityQueue::shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            is_shutdown_.store(true, std::memory_order_relaxed);
        }

        // Wake up all waiting threads
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();

        LOG_INFO("Queue '" + queue_name_ + "' shutdown initiated");
    }

    bool PriorityQueue::isShutdown() const
    {
        return is_shutdown_.load(std::memory_order_relaxed);
    }

    // Monitoring and statistics
    size_t PriorityQueue::size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool PriorityQueue::empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t PriorityQueue::capacity() const
    {
        return max_size_;
    }

    QueueStats PriorityQueue::getStats() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        QueueStats stats;
        stats.current_size = queue_.size();
        stats.max_size = max_size_;
        stats.total_pushed = total_pushed_.load(std::memory_order_relaxed);
        stats.total_popped = total_popped_.load(std::memory_order_relaxed);
        stats.total_dropped = total_dropped_.load(std::memory_order_relaxed);
        stats.peak_size = peak_size_.load(std::memory_order_relaxed);
        stats.avg_latency_ns = getAverageLatency();
        stats.overflow_policy_str = getOverflowPolicyString();

        return stats;
    }

    // Performance metrics
    uint64_t PriorityQueue::getTotalPushed() const
    {
        return total_pushed_.load(std::memory_order_relaxed);
    }

    uint64_t PriorityQueue::getTotalPopped() const
    {
        return total_popped_.load(std::memory_order_relaxed);
    }

    uint64_t PriorityQueue::getTotalDropped() const
    {
        return total_dropped_.load(std::memory_order_relaxed);
    }

    size_t PriorityQueue::getPeakSize() const
    {
        return peak_size_.load(std::memory_order_relaxed);
    }

    double PriorityQueue::getAverageLatency() const
    {
        uint64_t samples = latency_samples_.load(std::memory_order_relaxed);
        if (samples == 0)
            return 0.0;

        uint64_t total_latency = total_latency_ns_.load(std::memory_order_relaxed);
        return static_cast<double>(total_latency) / static_cast<double>(samples);
    }

    // Configuration
    void PriorityQueue::setOverflowPolicy(OverflowPolicy policy)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        overflow_policy_ = policy;
        LOG_INFO("Queue '" + queue_name_ + "' overflow policy changed to " + overflowPolicyToString(policy));
    }

    OverflowPolicy PriorityQueue::getOverflowPolicy() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return overflow_policy_;
    }

    void PriorityQueue::setMaxSize(size_t max_size)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        max_size_ = max_size;
        LOG_INFO("Queue '" + queue_name_ + "' max size changed to " + std::to_string(max_size));

        // If we're now over capacity, handle overflow
        while (queue_.size() > max_size_ && overflow_policy_ == OverflowPolicy::DROP_OLDEST)
        {
            queue_.pop();
            total_dropped_.fetch_add(1, std::memory_order_relaxed);
        }

        // Notify waiting producers
        not_full_cv_.notify_all();
    }

    // Utility methods
    std::string PriorityQueue::toString() const
    {
        std::ostringstream oss;
        auto stats = getStats();

        oss << "PriorityQueue{"
            << "name=" << queue_name_
            << ", size=" << stats.current_size
            << ", max_size=" << stats.max_size
            << ", pushed=" << stats.total_pushed
            << ", popped=" << stats.total_popped
            << ", dropped=" << stats.total_dropped
            << ", peak_size=" << stats.peak_size
            << ", avg_latency=" << std::fixed << std::setprecision(2) << stats.avg_latency_ns << "ns"
            << ", policy=" << stats.overflow_policy_str
            << ", shutdown=" << (is_shutdown_.load(std::memory_order_relaxed) ? "true" : "false")
            << "}";

        return oss.str();
    }

    std::string PriorityQueue::getOverflowPolicyString() const
    {
        return overflowPolicyToString(overflow_policy_);
    }

    // Helper methods
    bool PriorityQueue::pushInternal(MessagePtr message)
    {
        // This method assumes the mutex is already locked
        queue_.push(message);
        total_pushed_.fetch_add(1, std::memory_order_relaxed);

        // Update peak size
        size_t current_size = queue_.size();
        size_t current_peak = peak_size_.load(std::memory_order_relaxed);
        while (current_size > current_peak &&
               !peak_size_.compare_exchange_weak(current_peak, current_size, std::memory_order_relaxed))
        {
            // Loop until we successfully update peak_size or current_size <= current_peak
        }

        return true;
    }

    bool PriorityQueue::handleOverflow(MessagePtr message)
    {
        // This method assumes the mutex is already locked
        if (overflow_policy_ == OverflowPolicy::DROP_OLDEST && !queue_.empty())
        {
            queue_.pop(); // Remove oldest (lowest priority) message
            total_dropped_.fetch_add(1, std::memory_order_relaxed);
            LOG_DEBUG("Dropped oldest message due to queue overflow");
            return true;
        }
        return false;
    }

    void PriorityQueue::updateStats(MessagePtr message)
    {
        // Update message statistics - could be extended for more detailed tracking
        // For now, basic stats are handled in push/pop operations
    }

    void PriorityQueue::recordLatency(const std::chrono::steady_clock::time_point &start_time)
    {
        auto end_time = std::chrono::steady_clock::now();
        auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

        total_latency_ns_.fetch_add(latency_ns, std::memory_order_relaxed);
        latency_samples_.fetch_add(1, std::memory_order_relaxed);
    }

    std::string PriorityQueue::formatStats() const
    {
        auto stats = getStats();
        std::ostringstream oss;

        oss << "Queue Statistics for '" << queue_name_ << "':\n"
            << "  Current Size: " << stats.current_size << "/" << stats.max_size << "\n"
            << "  Total Pushed: " << stats.total_pushed << "\n"
            << "  Total Popped: " << stats.total_popped << "\n"
            << "  Total Dropped: " << stats.total_dropped << "\n"
            << "  Peak Size: " << stats.peak_size << "\n"
            << "  Average Latency: " << std::fixed << std::setprecision(2) << stats.avg_latency_ns << " ns\n"
            << "  Overflow Policy: " << stats.overflow_policy_str << "\n"
            << "  Utilization: " << std::fixed << std::setprecision(1)
            << (static_cast<double>(stats.current_size) / static_cast<double>(stats.max_size) * 100.0) << "%";

        return oss.str();
    }

    // Utility functions
    std::string overflowPolicyToString(OverflowPolicy policy)
    {
        switch (policy)
        {
        case OverflowPolicy::DROP_OLDEST:
            return "DROP_OLDEST";
        case OverflowPolicy::DROP_NEWEST:
            return "DROP_NEWEST";
        case OverflowPolicy::BLOCK:
            return "BLOCK";
        case OverflowPolicy::REJECT:
            return "REJECT";
        default:
            return "UNKNOWN";
        }
    }

    OverflowPolicy stringToOverflowPolicy(const std::string &policy_str)
    {
        if (policy_str == "DROP_OLDEST")
            return OverflowPolicy::DROP_OLDEST;
        if (policy_str == "DROP_NEWEST")
            return OverflowPolicy::DROP_NEWEST;
        if (policy_str == "BLOCK")
            return OverflowPolicy::BLOCK;
        if (policy_str == "REJECT")
            return OverflowPolicy::REJECT;
        return OverflowPolicy::DROP_OLDEST; // Default
    }

} // namespace fix_gateway::utils
