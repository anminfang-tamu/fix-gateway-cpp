#include "message_manager.h"
#include "utils/logger.h"
#include <iostream>
#include <sstream>
#include <unistd.h> // For getuid()
#include <cstring>  // For strerror

#ifdef __APPLE__
#include <pthread.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#include <pthread/qos.h>
#endif

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace fix_gateway::manager
{
    // Constructor with config
    MessageManager::MessageManager(const CorePinningConfig &config)
        : config_(config),
          running_(false),
          shutdown_requested_(false)
    {
        // Initialize core mapping
        priority_to_core_[Priority::CRITICAL] = config_.critical_core;
        priority_to_core_[Priority::HIGH] = config_.high_core;
        priority_to_core_[Priority::MEDIUM] = config_.medium_core;
        priority_to_core_[Priority::LOW] = config_.low_core;

        std::cout << "[MessageManager] Created with cores: "
                  << "CRITICAL=" << config_.critical_core
                  << ", HIGH=" << config_.high_core
                  << ", MEDIUM=" << config_.medium_core
                  << ", LOW=" << config_.low_core << std::endl;
    }

    // Default constructor
    MessageManager::MessageManager()
        : running_(false),
          shutdown_requested_(false)
    {
        // Initialize with default config values
        config_.critical_core = 0;
        config_.high_core = 1;
        config_.medium_core = 2;
        config_.low_core = 3;
        config_.enable_core_pinning = true;
        config_.enable_real_time_priority = false;
        config_.critical_queue_size = 1024;
        config_.high_queue_size = 2048;
        config_.medium_queue_size = 4096;
        config_.low_queue_size = 8192;

        // Initialize core mapping
        priority_to_core_[Priority::CRITICAL] = config_.critical_core;
        priority_to_core_[Priority::HIGH] = config_.high_core;
        priority_to_core_[Priority::MEDIUM] = config_.medium_core;
        priority_to_core_[Priority::LOW] = config_.low_core;

        std::cout << "[MessageManager] Created with default config" << std::endl;
    }

    // Destructor
    MessageManager::~MessageManager()
    {
        if (running_.load())
        {
            shutdown(std::chrono::seconds(5));
        }
        std::cout << "[MessageManager] Destroyed" << std::endl;
    }

    // Lifecycle management
    void MessageManager::start()
    {
        if (running_.load())
        {
            std::cout << "[MessageManager] Already running" << std::endl;
            return;
        }

        std::cout << "[MessageManager] Starting..." << std::endl;

        try
        {
            createTcpConnection();
            createQueuesAndSenders();
            startAsyncSenders();

            running_.store(true);
            std::cout << "[MessageManager] Started successfully" << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "[MessageManager] Failed to start: " << e.what() << std::endl;
            stop();
            throw;
        }
    }

    void MessageManager::stop()
    {
        if (!running_.load())
        {
            return;
        }

        std::cout << "[MessageManager] Stopping..." << std::endl;

        running_.store(false);
        stopAsyncSenders();

        // Disconnect TCP connection
        if (tcp_connection_)
        {
            tcp_connection_->disconnect();
        }

        std::cout << "[MessageManager] Stopped" << std::endl;
    }

    void MessageManager::shutdown(std::chrono::seconds timeout)
    {
        shutdown_requested_.store(true);
        std::cout << "[MessageManager] Shutdown requested with timeout: "
                  << timeout.count() << "s" << std::endl;

        stop();

        // Clean up resources
        priority_queues_.clear();
        async_senders_.clear();
        tcp_connection_.reset();

        shutdown_requested_.store(false);
        std::cout << "[MessageManager] Shutdown complete" << std::endl;
    }

    // Core message management functionality
    // dispatch message to the correct priority queue
    bool MessageManager::routeMessage(MessagePtr message)
    {
        if (!running_.load() || !message)
        {
            return false;
        }

        Priority priority = getMessagePriority(message);
        auto queue = getQueueForPriority(priority);

        if (!queue)
        {
            std::cerr << "[MessageManager] No queue found for priority: "
                      << static_cast<int>(priority) << std::endl;
            return false;
        }

        bool success = queue->push(message);
        if (!success)
        {
            std::cerr << "[MessageManager] Failed to route message to "
                      << static_cast<int>(priority) << " priority queue" << std::endl;
        }

        return success;
    }

    // Status and monitoring
    bool MessageManager::isRunning() const
    {
        return running_.load();
    }

    bool MessageManager::isConnected() const
    {
        return tcp_connection_ && tcp_connection_->isConnected();
    }

    MessageManager::PerformanceStats MessageManager::getStats() const
    {
        PerformanceStats stats;

        // Collect per-priority stats
        for (const auto &[priority, sender] : async_senders_)
        {
            if (sender)
            {
                stats.priority_stats[priority] = sender->getStats();
            }
        }

        // Calculate aggregate metrics
        for (const auto &[priority, priority_stats] : stats.priority_stats)
        {
            stats.total_messages_sent += priority_stats.total_messages_sent;
            stats.total_messages_failed += priority_stats.total_messages_failed;

            // Count by priority
            switch (priority)
            {
            case Priority::CRITICAL:
                stats.critical_count = priority_stats.total_messages_sent;
                stats.avg_critical_latency_ns = priority_stats.avg_send_latency_ns;
                break;
            case Priority::HIGH:
                stats.high_count = priority_stats.total_messages_sent;
                break;
            case Priority::MEDIUM:
                stats.medium_count = priority_stats.total_messages_sent;
                break;
            case Priority::LOW:
                stats.low_count = priority_stats.total_messages_sent;
                break;
            }
        }

        // TCP connection status
        stats.tcp_connected = isConnected();
        if (tcp_connection_)
        {
            stats.last_connection_time = std::chrono::steady_clock::now();
        }

        return stats;
    }

    SenderStats MessageManager::getStatsForPriority(Priority priority) const
    {
        auto it = async_senders_.find(priority);
        if (it != async_senders_.end() && it->second)
        {
            return it->second->getStats();
        }
        return SenderStats{}; // Return empty stats if not found
    }

    // Configuration
    void MessageManager::setCoreAffinity(Priority priority, int core_id)
    {
        priority_to_core_[priority] = core_id;
        std::cout << "[MessageManager] Set core affinity for priority "
                  << static_cast<int>(priority) << " to core " << core_id << std::endl;
    }

    void MessageManager::setRealTimePriority(Priority priority, bool enable)
    {
        // Update config
        config_.enable_real_time_priority = enable;
        std::cout << "[MessageManager] Set real-time priority for priority "
                  << static_cast<int>(priority) << " to " << (enable ? "enabled" : "disabled") << std::endl;
    }

    // Monitoring
    std::vector<size_t> MessageManager::getQueueDepths() const
    {
        std::vector<size_t> depths;
        depths.reserve(priority_queues_.size());

        for (const auto &[priority, queue] : priority_queues_)
        {
            depths.push_back(queue ? queue->size() : 0);
        }

        return depths;
    }

    size_t MessageManager::getQueueDepthForPriority(Priority priority) const
    {
        auto queue = getQueueForPriority(priority);
        return queue ? queue->size() : 0;
    }

    bool MessageManager::areAllCoresConnected() const
    {
        if (!isConnected())
        {
            return false;
        }

        // Check if all AsyncSenders are running
        for (const auto &[priority, sender] : async_senders_)
        {
            if (!sender || !sender->isRunning())
            {
                return false;
            }
        }

        return true;
    }

    // Connection management
    bool MessageManager::connectToServer(const std::string &host, int port)
    {
        if (!tcp_connection_)
        {
            createTcpConnection();
        }

        bool success = tcp_connection_->connect(host, port);
        if (success)
        {
            std::cout << "[MessageManager] Connected to " << host << ":" << port << std::endl;
        }
        else
        {
            std::cerr << "[MessageManager] Failed to connect to " << host << ":" << port << std::endl;
        }

        return success;
    }

    void MessageManager::disconnectFromServer()
    {
        if (tcp_connection_)
        {
            tcp_connection_->disconnect();
            std::cout << "[MessageManager] Disconnected from server" << std::endl;
        }
    }

    // Private helper methods
    void MessageManager::createTcpConnection()
    {
        tcp_connection_ = std::make_shared<TcpConnection>();
        std::cout << "[MessageManager] Created shared TCP connection" << std::endl;
    }

    void MessageManager::createQueuesAndSenders()
    {
        std::cout << "[MessageManager] Creating queues and senders..." << std::endl;

        // Create priority queues
        priority_queues_[Priority::CRITICAL] = std::make_shared<PriorityQueue>(
            config_.critical_queue_size, fix_gateway::utils::OverflowPolicy::DROP_OLDEST, "critical_queue");

        priority_queues_[Priority::HIGH] = std::make_shared<PriorityQueue>(
            config_.high_queue_size, fix_gateway::utils::OverflowPolicy::DROP_OLDEST, "high_queue");

        priority_queues_[Priority::MEDIUM] = std::make_shared<PriorityQueue>(
            config_.medium_queue_size, fix_gateway::utils::OverflowPolicy::DROP_OLDEST, "medium_queue");

        priority_queues_[Priority::LOW] = std::make_shared<PriorityQueue>(
            config_.low_queue_size, fix_gateway::utils::OverflowPolicy::DROP_OLDEST, "low_queue");

        // Create AsyncSenders with shared TCP connection
        for (const auto &[priority, queue] : priority_queues_)
        {
            async_senders_[priority] = std::make_unique<AsyncSender>(queue, tcp_connection_);
        }

        std::cout << "[MessageManager] Created " << priority_queues_.size()
                  << " queues and " << async_senders_.size() << " senders" << std::endl;
    }

    void MessageManager::startAsyncSenders()
    {
        std::cout << "[MessageManager] Starting AsyncSenders..." << std::endl;

        // Print system information
        int detected_cores = MessageManagerFactory::detectPerformanceCores();
        std::cout << "[MessageManager] Detected " << detected_cores << " performance cores" << std::endl;

#ifdef __APPLE__
        std::cout << "[MessageManager] macOS detected - thread affinity support is limited" << std::endl;
        std::cout << "[MessageManager] Will attempt affinity first, then fallback to QoS classes" << std::endl;
#elif defined(__linux__)
        std::cout << "[MessageManager] Linux detected - full thread affinity and RT priority support" << std::endl;
        std::cout << "[MessageManager] Can pin threads to specific cores with pthread_setaffinity_np" << std::endl;
#else
        std::cout << "[MessageManager] Platform has limited thread control capabilities" << std::endl;
#endif

        for (auto &[priority, sender] : async_senders_)
        {
            if (sender)
            {
                sender->start();

                // Pin thread to dedicated core
                if (config_.enable_core_pinning && sender->isThreadJoinable())
                {
                    int core_id = getCoreForPriority(priority);
                    std::thread &sender_thread = sender->getSenderThread();

                    std::cout << "[MessageManager] Attempting to pin priority "
                              << static_cast<int>(priority) << " thread to core " << core_id << std::endl;

                    bool pinned = pinThreadToCore(sender_thread, core_id);
                    if (pinned)
                    {
                        std::cout << "[MessageManager] Successfully configured priority "
                                  << static_cast<int>(priority) << " thread for core " << core_id << std::endl;
                    }
                    else
                    {
                        std::cout << "[MessageManager] Thread configuration failed for priority "
                                  << static_cast<int>(priority) << " thread (core " << core_id
                                  << ") - performance may be reduced" << std::endl;
                    }

                    // Set real-time priority if enabled
                    if (config_.enable_real_time_priority)
                    {
                        bool rt_set = setThreadRealTimePriority(sender_thread);
                        if (rt_set)
                        {
                            std::cout << "[MessageManager] Set real-time priority for "
                                      << static_cast<int>(priority) << " thread" << std::endl;
                        }
                        else
                        {
                            std::cout << "[MessageManager] Failed to set real-time priority for "
                                      << static_cast<int>(priority) << " thread (try running as root)" << std::endl;
                        }
                    }
                }
                else
                {
                    std::cout << "[MessageManager] Core pinning disabled for priority "
                              << static_cast<int>(priority) << " thread" << std::endl;
                }
            }
        }

        std::cout << "[MessageManager] All AsyncSenders started" << std::endl;
    }

    void MessageManager::stopAsyncSenders()
    {
        std::cout << "[MessageManager] Stopping AsyncSenders..." << std::endl;

        for (auto &[priority, sender] : async_senders_)
        {
            if (sender && sender->isRunning())
            {
                sender->stop();
            }
        }

        std::cout << "[MessageManager] All AsyncSenders stopped" << std::endl;
    }

    bool MessageManager::pinThreadToCore(std::thread &thread, int core_id)
    {
        // Platform-specific implementation
#ifdef __APPLE__
        // macOS thread affinity (limited support)
        thread_affinity_policy_data_t policy = {static_cast<integer_t>(core_id)};
        thread_port_t mach_thread = pthread_mach_thread_np(thread.native_handle());

        kern_return_t result = thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                                                 (thread_policy_t)&policy, 1);

        if (result != KERN_SUCCESS)
        {
            // Get more detailed error information
            const char *error_msg = "Unknown error";
            switch (result)
            {
            case KERN_INVALID_ARGUMENT:
                error_msg = "Invalid argument (core may not exist)";
                break;
            case KERN_INVALID_POLICY:
                error_msg = "Invalid policy (affinity not supported)";
                break;
            case KERN_NOT_SUPPORTED:
                error_msg = "Not supported on this system";
                break;
            case KERN_FAILURE:
                error_msg = "General failure";
                break;
            case KERN_RESOURCE_SHORTAGE:
                error_msg = "Resource shortage";
                break;
            default:
                error_msg = "Unknown kern_return_t error";
                break;
            }

            std::cerr << "[MessageManager] Failed to set thread affinity to core "
                      << core_id << " - " << error_msg << " (kern_return_t: " << result << ")" << std::endl;

            // Try alternative approach: QoS class
            std::cout << "[MessageManager] Attempting alternative QoS-based approach for core " << core_id << std::endl;
            return setThreadQoSClass(thread, core_id);
        }

        std::cout << "[MessageManager] Successfully pinned thread to core " << core_id << std::endl;
        return true;
#elif defined(__linux__)
        // Linux implementation using pthread_setaffinity_np
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);

        pthread_t pthread_handle = thread.native_handle();
        int result = pthread_setaffinity_np(pthread_handle, sizeof(cpu_set_t), &cpuset);

        if (result != 0)
        {
            std::cerr << "[MessageManager] Failed to set thread affinity to core " << core_id
                      << " - error: " << result << " (" << strerror(result) << ")" << std::endl;
            return false;
        }

        std::cout << "[MessageManager] Successfully pinned thread to core " << core_id << std::endl;
        return true;
#else
        std::cout << "[MessageManager] Thread pinning not implemented for this platform" << std::endl;
        return false;
#endif
    }

    bool MessageManager::setThreadQoSClass(std::thread &thread, int core_id)
    {
#ifdef __APPLE__
        // Alternative approach using QoS classes for macOS
        // This provides better scheduling hints than direct affinity

        pthread_t pthread_handle = thread.native_handle();

        // Set QoS class based on priority level
        qos_class_t qos_class;
        int relative_priority = 0;

        // Map core assignments to QoS classes
        switch (core_id)
        {
        case 0: // Critical priority
            qos_class = QOS_CLASS_USER_INTERACTIVE;
            relative_priority = QOS_MIN_RELATIVE_PRIORITY; // Highest priority
            break;
        case 1: // High priority
            qos_class = QOS_CLASS_USER_INTERACTIVE;
            relative_priority = -10;
            break;
        case 2: // Medium priority
            qos_class = QOS_CLASS_USER_INITIATED;
            relative_priority = 0;
            break;
        case 3: // Low priority
            qos_class = QOS_CLASS_DEFAULT;
            relative_priority = 0;
            break;
        default:
            qos_class = QOS_CLASS_DEFAULT;
            relative_priority = 0;
            break;
        }

        int result = pthread_set_qos_class_self_np(qos_class, relative_priority);

        if (result != 0)
        {
            std::cerr << "[MessageManager] Failed to set QoS class for core " << core_id
                      << " - error: " << result << std::endl;
            return false;
        }

        std::cout << "[MessageManager] Set QoS class for core " << core_id
                  << " (QoS: " << qos_class << ", priority: " << relative_priority << ")" << std::endl;
        return true;
#else
        return false;
#endif
    }

    bool MessageManager::setThreadRealTimePriority(std::thread &thread)
    {
        if (!config_.enable_real_time_priority)
        {
            return false;
        }

        // Platform-specific real-time priority implementation
#ifdef __APPLE__
        // macOS real-time thread priority
        thread_time_constraint_policy_data_t policy;
        policy.period = 1000000;     // 1ms
        policy.computation = 500000; // 0.5ms
        policy.constraint = 1000000; // 1ms
        policy.preemptible = 0;

        thread_port_t mach_thread = pthread_mach_thread_np(thread.native_handle());
        kern_return_t result = thread_policy_set(mach_thread, THREAD_TIME_CONSTRAINT_POLICY,
                                                 (thread_policy_t)&policy,
                                                 THREAD_TIME_CONSTRAINT_POLICY_COUNT);

        return result == KERN_SUCCESS;
#elif defined(__linux__)
        // Linux real-time priority implementation
        pthread_t pthread_handle = thread.native_handle();

        // Set real-time scheduling policy
        struct sched_param param;
        param.sched_priority = 99; // Maximum RT priority (1-99)

        int result = pthread_setschedparam(pthread_handle, SCHED_FIFO, &param);

        if (result != 0)
        {
            std::cerr << "[MessageManager] Failed to set real-time priority - error: "
                      << result << " (" << strerror(result) << ")" << std::endl;
            std::cerr << "[MessageManager] Note: Real-time priority requires CAP_SYS_NICE capability" << std::endl;
            return false;
        }

        std::cout << "[MessageManager] Set real-time priority (SCHED_FIFO, priority=99)" << std::endl;
        return true;
#else
        std::cout << "[MessageManager] Real-time priority not implemented for this platform" << std::endl;
        return false;
#endif
    }

    Priority MessageManager::getMessagePriority(MessagePtr message) const
    {
        return message ? message->getPriority() : Priority::LOW;
    }

    std::shared_ptr<PriorityQueue> MessageManager::getQueueForPriority(Priority priority) const
    {
        auto it = priority_queues_.find(priority);
        return (it != priority_queues_.end()) ? it->second : nullptr;
    }

    size_t MessageManager::getQueueSizeForPriority(Priority priority) const
    {
        switch (priority)
        {
        case Priority::CRITICAL:
            return config_.critical_queue_size;
        case Priority::HIGH:
            return config_.high_queue_size;
        case Priority::MEDIUM:
            return config_.medium_queue_size;
        case Priority::LOW:
            return config_.low_queue_size;
        default:
            return config_.low_queue_size;
        }
    }

    int MessageManager::getCoreForPriority(Priority priority) const
    {
        auto it = priority_to_core_.find(priority);
        return (it != priority_to_core_.end()) ? it->second : 0;
    }

    // MessageManagerFactory Implementation
    MessageManager::CorePinningConfig MessageManagerFactory::createM1MaxConfig()
    {
        MessageManager::CorePinningConfig config;

        // M1 Max has 8 performance cores + 2 efficiency cores
        // Use performance cores for trading
        config.critical_core = 0; // First performance core
        config.high_core = 1;     // Second performance core
        config.medium_core = 2;   // Third performance core
        config.low_core = 3;      // Fourth performance core

        // macOS has limited thread affinity support - use QoS classes instead
        config.enable_core_pinning = true;        // Will fallback to QoS if affinity fails
        config.enable_real_time_priority = false; // Requires root and often fails

        // Optimized queue sizes for M1 Max
        config.critical_queue_size = 512; // Small for ultra-low latency
        config.high_queue_size = 1024;    // Medium for high priority
        config.medium_queue_size = 2048;  // Larger for medium priority
        config.low_queue_size = 4096;     // Largest for low priority

        return config;
    }

    MessageManager::CorePinningConfig MessageManagerFactory::createIntelConfig()
    {
        MessageManager::CorePinningConfig config;

        // Generic Intel configuration
        config.critical_core = 0;
        config.high_core = 1;
        config.medium_core = 2;
        config.low_core = 3;

        config.enable_core_pinning = true;
        config.enable_real_time_priority = false;

        // Conservative queue sizes
        config.critical_queue_size = 1024;
        config.high_queue_size = 2048;
        config.medium_queue_size = 4096;
        config.low_queue_size = 8192;

        return config;
    }

    MessageManager::CorePinningConfig MessageManagerFactory::createDefaultConfig()
    {
        MessageManager::CorePinningConfig config;

        // Safe defaults that work everywhere
        config.critical_core = 0;
        config.high_core = 1;
        config.medium_core = 2;
        config.low_core = 3;

        config.enable_core_pinning = false; // Disabled by default
        config.enable_real_time_priority = false;

        // Default queue sizes
        config.critical_queue_size = 1024;
        config.high_queue_size = 2048;
        config.medium_queue_size = 4096;
        config.low_queue_size = 8192;

        return config;
    }

    MessageManager::CorePinningConfig MessageManagerFactory::createLowLatencyConfig()
    {
        auto config = createM1MaxConfig(); // Start with hardware-optimized

        // Optimize for ultra-low latency
        config.critical_queue_size = 256; // Smaller queues
        config.high_queue_size = 512;
        config.medium_queue_size = 1024;
        config.low_queue_size = 2048;

        config.enable_real_time_priority = true; // Requires root

        return config;
    }

    MessageManager::CorePinningConfig MessageManagerFactory::createHighThroughputConfig()
    {
        auto config = createM1MaxConfig(); // Start with hardware-optimized

        // Optimize for maximum throughput
        config.critical_queue_size = 2048; // Larger queues
        config.high_queue_size = 4096;
        config.medium_queue_size = 8192;
        config.low_queue_size = 16384;

        return config;
    }

    int MessageManagerFactory::detectPerformanceCores()
    {
#ifdef __APPLE__
        // M1 Max typically has 8 performance cores
        return 8;
#else
        // Conservative estimate for other platforms
        return std::thread::hardware_concurrency();
#endif
    }

    std::vector<int> MessageManagerFactory::getOptimalCoreAssignment()
    {
        int num_cores = detectPerformanceCores();
        std::vector<int> assignment;

        // Assign first 4 cores to priorities
        for (int i = 0; i < std::min(4, num_cores); ++i)
        {
            assignment.push_back(i);
        }

        return assignment;
    }

    bool MessageManagerFactory::isRealTimePrioritySupported()
    {
        // Real-time priority typically requires root privileges
        return getuid() == 0; // Check if running as root
    }

} // namespace fix_gateway::manager