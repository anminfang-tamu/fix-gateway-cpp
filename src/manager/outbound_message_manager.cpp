#include "outbound_message_manager.h"
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
    OutboundMessageManager::OutboundMessageManager(const CorePinningConfig &config)
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
    OutboundMessageManager::OutboundMessageManager()
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
    OutboundMessageManager::~OutboundMessageManager()
    {
        if (running_.load())
        {
            shutdown(std::chrono::seconds(5));
        }
        std::cout << "[MessageManager] Destroyed" << std::endl;
    }

    // Lifecycle management
    void OutboundMessageManager::start()
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

    void OutboundMessageManager::stop()
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

    void OutboundMessageManager::shutdown(std::chrono::seconds timeout)
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
    bool OutboundMessageManager::routeMessage(MessagePtr message)
    {
        if (!running_.load() || !message)
        {
            return false;
        }

        Priority priority = getMessagePriority(message);
        bool success = pushToQueue(priority, message);

        if (!success)
        {
            std::cerr << "[MessageManager] Failed to route message to "
                      << static_cast<int>(priority) << " priority queue" << std::endl;
        }

        return success;
    }

    // Status and monitoring
    bool OutboundMessageManager::isRunning() const
    {
        return running_.load();
    }

    bool OutboundMessageManager::isConnected() const
    {
        return tcp_connection_ && tcp_connection_->isConnected();
    }

    OutboundMessageManager::PerformanceStats OutboundMessageManager::getStats() const
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

        // Queue type information
        stats.queue_type = config_.queue_type;
        stats.queue_type_string = getQueueTypeString();

        return stats;
    }

    SenderStats OutboundMessageManager::getStatsForPriority(Priority priority) const
    {
        auto it = async_senders_.find(priority);
        if (it != async_senders_.end() && it->second)
        {
            return it->second->getStats();
        }
        return SenderStats{}; // Return empty stats if not found
    }

    // Configuration
    void OutboundMessageManager::setCoreAffinity(Priority priority, int core_id)
    {
        priority_to_core_[priority] = core_id;
        std::cout << "[MessageManager] Set core affinity for priority "
                  << static_cast<int>(priority) << " to core " << core_id << std::endl;
    }

    void OutboundMessageManager::setRealTimePriority(Priority priority, bool enable)
    {
        // Update config
        config_.enable_real_time_priority = enable;
        std::cout << "[MessageManager] Set real-time priority for priority "
                  << static_cast<int>(priority) << " to " << (enable ? "enabled" : "disabled") << std::endl;
    }

    // Monitoring
    std::vector<size_t> OutboundMessageManager::getQueueDepths() const
    {
        std::vector<size_t> depths;
        depths.reserve(4); // 4 priority levels

        depths.push_back(getQueueSize(Priority::CRITICAL));
        depths.push_back(getQueueSize(Priority::HIGH));
        depths.push_back(getQueueSize(Priority::MEDIUM));
        depths.push_back(getQueueSize(Priority::LOW));

        return depths;
    }

    size_t OutboundMessageManager::getQueueDepthForPriority(Priority priority) const
    {
        return getQueueSize(priority);
    }

    bool OutboundMessageManager::areAllCoresConnected() const
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
    bool OutboundMessageManager::connectToServer(const std::string &host, int port)
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

    void OutboundMessageManager::disconnectFromServer()
    {
        if (tcp_connection_)
        {
            tcp_connection_->disconnect();
            std::cout << "[MessageManager] Disconnected from server" << std::endl;
        }
    }

    // Private helper methods
    void OutboundMessageManager::createTcpConnection()
    {
        tcp_connection_ = std::make_shared<TcpConnection>();
        std::cout << "[MessageManager] Created shared TCP connection" << std::endl;
    }

    void OutboundMessageManager::createQueuesAndSenders()
    {
        std::cout << "[MessageManager] Creating queues and senders..." << std::endl;
        std::cout << "[MessageManager] Queue type: " << getQueueTypeString() << std::endl;

        if (config_.queue_type == QueueType::MUTEX_BASED)
        {
            // Create mutex-based priority queues
            priority_queues_[Priority::CRITICAL] = std::make_shared<PriorityQueue>(
                config_.critical_queue_size, fix_gateway::utils::OverflowPolicy::DROP_OLDEST, "critical_queue");

            priority_queues_[Priority::HIGH] = std::make_shared<PriorityQueue>(
                config_.high_queue_size, fix_gateway::utils::OverflowPolicy::DROP_OLDEST, "high_queue");

            priority_queues_[Priority::MEDIUM] = std::make_shared<PriorityQueue>(
                config_.medium_queue_size, fix_gateway::utils::OverflowPolicy::DROP_OLDEST, "medium_queue");

            priority_queues_[Priority::LOW] = std::make_shared<PriorityQueue>(
                config_.low_queue_size, fix_gateway::utils::OverflowPolicy::DROP_OLDEST, "low_queue");

            // Create AsyncSenders with mutex-based queues
            for (const auto &[priority, queue] : priority_queues_)
            {
                async_senders_[priority] = std::make_unique<AsyncSender>(queue, tcp_connection_);
            }

            std::cout << "[MessageManager] Created " << priority_queues_.size()
                      << " mutex-based queues and " << async_senders_.size() << " senders" << std::endl;
        }
        else // LOCK_FREE
        {
            // Create lock-free priority queues
            lockfree_queues_[Priority::CRITICAL] = std::make_shared<LockFreeQueue>(
                config_.critical_queue_size, "critical_lockfree_queue");

            lockfree_queues_[Priority::HIGH] = std::make_shared<LockFreeQueue>(
                config_.high_queue_size, "high_lockfree_queue");

            lockfree_queues_[Priority::MEDIUM] = std::make_shared<LockFreeQueue>(
                config_.medium_queue_size, "medium_lockfree_queue");

            lockfree_queues_[Priority::LOW] = std::make_shared<LockFreeQueue>(
                config_.low_queue_size, "low_lockfree_queue");

            // Create AsyncSenders with lock-free queues
            for (const auto &[priority, queue] : lockfree_queues_)
            {
                async_senders_[priority] = std::make_unique<AsyncSender>(queue, tcp_connection_);
            }

            std::cout << "[MessageManager] Created " << lockfree_queues_.size()
                      << " lock-free queues and " << async_senders_.size() << " senders" << std::endl;
        }
    }

    void OutboundMessageManager::startAsyncSenders()
    {
        std::cout << "[MessageManager] Starting AsyncSenders..." << std::endl;

        // Print system information
        int detected_cores = OutboundMessageManagerFactory::detectPerformanceCores();
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

    void OutboundMessageManager::stopAsyncSenders()
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

    bool OutboundMessageManager::pinThreadToCore(std::thread &thread, int core_id)
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

    bool OutboundMessageManager::setThreadQoSClass(std::thread &thread, int core_id)
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

    bool OutboundMessageManager::setThreadRealTimePriority(std::thread &thread)
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

    Priority OutboundMessageManager::getMessagePriority(MessagePtr message) const
    {
        return message ? message->getPriority() : Priority::LOW;
    }

    // Queue interface abstraction methods
    bool OutboundMessageManager::pushToQueue(Priority priority, MessagePtr message)
    {
        if (config_.queue_type == QueueType::MUTEX_BASED)
        {
            auto it = priority_queues_.find(priority);
            if (it != priority_queues_.end() && it->second)
            {
                return it->second->push(message);
            }
        }
        else // LOCK_FREE
        {
            auto it = lockfree_queues_.find(priority);
            if (it != lockfree_queues_.end() && it->second)
            {
                return it->second->push(message);
            }
        }
        return false;
    }

    size_t OutboundMessageManager::getQueueSize(Priority priority) const
    {
        if (config_.queue_type == QueueType::MUTEX_BASED)
        {
            auto it = priority_queues_.find(priority);
            return (it != priority_queues_.end() && it->second) ? it->second->size() : 0;
        }
        else // LOCK_FREE
        {
            auto it = lockfree_queues_.find(priority);
            return (it != lockfree_queues_.end() && it->second) ? it->second->size() : 0;
        }
    }

    std::string OutboundMessageManager::getQueueTypeString() const
    {
        switch (config_.queue_type)
        {
        case QueueType::MUTEX_BASED:
            return "MUTEX_BASED";
        case QueueType::LOCK_FREE:
            return "LOCK_FREE";
        default:
            return "UNKNOWN";
        }
    }

    size_t OutboundMessageManager::getQueueSizeForPriority(Priority priority) const
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

    int OutboundMessageManager::getCoreForPriority(Priority priority) const
    {
        auto it = priority_to_core_.find(priority);
        return (it != priority_to_core_.end()) ? it->second : 0;
    }

    // MessageManagerFactory Implementation
    OutboundMessageManager::CorePinningConfig OutboundMessageManagerFactory::createM1MaxConfig()
    {
        OutboundMessageManager::CorePinningConfig config;

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

    OutboundMessageManager::CorePinningConfig OutboundMessageManagerFactory::createIntelConfig()
    {
        OutboundMessageManager::CorePinningConfig config;

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

    OutboundMessageManager::CorePinningConfig OutboundMessageManagerFactory::createDefaultConfig()
    {
        OutboundMessageManager::CorePinningConfig config;

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

    OutboundMessageManager::CorePinningConfig OutboundMessageManagerFactory::createLowLatencyConfig()
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

    OutboundMessageManager::CorePinningConfig OutboundMessageManagerFactory::createHighThroughputConfig()
    {
        auto config = createM1MaxConfig(); // Start with hardware-optimized

        // Optimize for maximum throughput
        config.critical_queue_size = 2048; // Larger queues
        config.high_queue_size = 4096;
        config.medium_queue_size = 8192;
        config.low_queue_size = 16384;

        return config;
    }

    OutboundMessageManager::CorePinningConfig OutboundMessageManagerFactory::createLockFreeConfig()
    {
        auto config = createDefaultConfig();

        // Enable lock-free queues
        config.queue_type = OutboundMessageManager::QueueType::LOCK_FREE;

        // Optimize queue sizes for lock-free (power of 2)
        config.critical_queue_size = 512; // 2^9
        config.high_queue_size = 1024;    // 2^10
        config.medium_queue_size = 2048;  // 2^11
        config.low_queue_size = 4096;     // 2^12

        return config;
    }

    OutboundMessageManager::CorePinningConfig OutboundMessageManagerFactory::createLockFreeM1MaxConfig()
    {
        auto config = createM1MaxConfig(); // Start with M1 Max hardware config

        // Enable lock-free queues
        config.queue_type = OutboundMessageManager::QueueType::LOCK_FREE;

        // Optimize for M1 Max with lock-free queues
        config.critical_queue_size = 256; // 2^8 - Ultra low latency
        config.high_queue_size = 512;     // 2^9 - High performance
        config.medium_queue_size = 1024;  // 2^10 - Medium performance
        config.low_queue_size = 2048;     // 2^11 - Background tasks

        // Enable advanced features for maximum performance
        config.enable_core_pinning = true;
        config.enable_real_time_priority = true; // Requires root

        return config;
    }

    int OutboundMessageManagerFactory::detectPerformanceCores()
    {
#ifdef __APPLE__
        // M1 Max typically has 8 performance cores
        return 8;
#else
        // Conservative estimate for other platforms
        return std::thread::hardware_concurrency();
#endif
    }

    std::vector<int> OutboundMessageManagerFactory::getOptimalCoreAssignment()
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

    bool OutboundMessageManagerFactory::isRealTimePrioritySupported()
    {
        // Real-time priority typically requires root privileges
        return getuid() == 0; // Check if running as root
    }

} // namespace fix_gateway::manager