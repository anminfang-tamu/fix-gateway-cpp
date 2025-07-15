#include "message_manager.h"
#include "utils/logger.h"
#include <iostream>
#include <sstream>
#include <unistd.h> // For getuid()

#ifdef __APPLE__
#include <pthread.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
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

                    bool pinned = pinThreadToCore(sender_thread, core_id);
                    if (pinned)
                    {
                        std::cout << "[MessageManager] Pinned priority "
                                  << static_cast<int>(priority) << " thread to core " << core_id << std::endl;
                    }
                    else
                    {
                        std::cerr << "[MessageManager] Failed to pin priority "
                                  << static_cast<int>(priority) << " thread to core " << core_id << std::endl;
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
                    }
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
        thread_affinity_policy_data_t policy = {core_id};
        thread_port_t mach_thread = pthread_mach_thread_np(thread.native_handle());

        kern_return_t result = thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                                                 (thread_policy_t)&policy, 1);

        if (result != KERN_SUCCESS)
        {
            std::cerr << "[MessageManager] Failed to set thread affinity to core "
                      << core_id << std::endl;
            return false;
        }

        std::cout << "[MessageManager] Pinned thread to core " << core_id << std::endl;
        return true;
#else
        // Linux implementation would go here
        std::cout << "[MessageManager] Thread pinning not implemented for this platform" << std::endl;
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
#else
        // Linux implementation would go here
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

        config.enable_core_pinning = true;
        config.enable_real_time_priority = false; // Requires root

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