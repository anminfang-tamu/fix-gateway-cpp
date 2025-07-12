#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <thread>

namespace fix_gateway::utils
{
    /**
     * @brief Thread-safe counter for tracking numeric metrics
     *
     * Optimized for high-frequency updates in trading systems.
     * Uses atomic operations for minimal contention.
     */
    class AtomicCounter
    {
    public:
        AtomicCounter() : value_(0) {}
        explicit AtomicCounter(uint64_t initial) : value_(initial) {}

        // Increment operations
        uint64_t increment() noexcept { return ++value_; }
        uint64_t add(uint64_t delta) noexcept { return value_ += delta; }

        // Decrement operations
        uint64_t decrement() noexcept { return --value_; }
        uint64_t subtract(uint64_t delta) noexcept { return value_ -= delta; }

        // Read operations
        uint64_t get() const noexcept { return value_.load(); }
        uint64_t reset() noexcept { return value_.exchange(0); }

        // Set operations
        void set(uint64_t new_value) noexcept { value_.store(new_value); }

    private:
        std::atomic<uint64_t> value_;
    };

    /**
     * @brief Rate tracker for measuring throughput (e.g., messages/second)
     *
     * Tracks events over time windows to calculate rates.
     * Thread-safe with minimal locking.
     */
    class RateTracker
    {
    public:
        explicit RateTracker(std::chrono::seconds window_duration = std::chrono::seconds(1));

        /**
         * @brief Record an event occurrence
         * @param count Number of events (default 1)
         */
        void recordEvent(uint64_t count = 1);

        /**
         * @brief Get current rate (events per second)
         * @return Current rate as double
         */
        double getCurrentRate();

        /**
         * @brief Get total events since creation
         * @return Total event count
         */
        uint64_t getTotalEvents() const { return total_events_.get(); }

        /**
         * @brief Reset all counters
         */
        void reset();

    private:
        std::chrono::seconds window_duration_;
        AtomicCounter total_events_;
        mutable std::mutex rate_mutex_;
        std::chrono::steady_clock::time_point last_calculation_;
        uint64_t events_in_window_;
        double current_rate_;
    };

    /**
     * @brief Gauge for tracking current values (e.g., queue depth, memory usage)
     *
     * Thread-safe gauge that can be updated from multiple threads.
     */
    class Gauge
    {
    public:
        Gauge() : value_(0) {}
        explicit Gauge(double initial) : value_(initial) {}

        void set(double value) { value_.store(value); }
        double get() const { return value_.load(); }

        void increment(double delta = 1.0)
        {
            double current = value_.load();
            while (!value_.compare_exchange_weak(current, current + delta))
            {
            }
        }

        void decrement(double delta = 1.0)
        {
            double current = value_.load();
            while (!value_.compare_exchange_weak(current, current - delta))
            {
            }
        }

    private:
        std::atomic<double> value_;
    };

    /**
     * @brief Central registry for all performance metrics
     *
     * Singleton that manages counters, gauges, and rates.
     * Provides centralized access and reporting.
     */
    class PerformanceCounters
    {
    public:
        static PerformanceCounters &getInstance();

        // Counter management
        AtomicCounter &getCounter(const std::string &name);
        void incrementCounter(const std::string &name, uint64_t delta = 1);
        uint64_t getCounterValue(const std::string &name);

        // Rate tracking
        RateTracker &getRateTracker(const std::string &name);
        void recordRate(const std::string &name, uint64_t count = 1);
        double getRateValue(const std::string &name);

        // Gauge management
        Gauge &getGauge(const std::string &name);
        void setGauge(const std::string &name, double value);
        double getGaugeValue(const std::string &name);

        // Reporting
        void printReport(const std::string &title = "Performance Counters") const;
        std::unordered_map<std::string, uint64_t> getAllCounters() const;
        std::unordered_map<std::string, double> getAllRates();
        std::unordered_map<std::string, double> getAllGauges() const;

        // Reset all metrics
        void reset();

    private:
        PerformanceCounters() = default;

        mutable std::mutex counters_mutex_;
        mutable std::mutex rates_mutex_;
        mutable std::mutex gauges_mutex_;

        std::unordered_map<std::string, std::unique_ptr<AtomicCounter>> counters_;
        std::unordered_map<std::string, std::unique_ptr<RateTracker>> rate_trackers_;
        std::unordered_map<std::string, std::unique_ptr<Gauge>> gauges_;
    };

    /**
     * @brief System resource monitor
     *
     * Tracks CPU usage, memory consumption, and other system metrics.
     * Updates gauges automatically in background thread.
     */
    class SystemMonitor
    {
    public:
        explicit SystemMonitor(std::chrono::seconds update_interval = std::chrono::seconds(1));
        ~SystemMonitor();

        void start();
        void stop();
        bool isRunning() const { return running_; }

        // Get current system metrics
        double getCpuUsage() const;
        uint64_t getMemoryUsageMB() const;
        uint32_t getThreadCount() const;

    private:
        void monitorLoop();
        void updateCpuUsage();
        void updateMemoryUsage();
        void updateThreadCount();

        std::chrono::seconds update_interval_;
        std::atomic<bool> running_;
        std::thread monitor_thread_;

        mutable std::mutex metrics_mutex_;
        double cpu_usage_;
        uint64_t memory_usage_mb_;
        uint32_t thread_count_;
    };

// Convenience macros for easy metric tracking
#define PERF_COUNTER_INC(name) \
    fix_gateway::utils::PerformanceCounters::getInstance().incrementCounter(name)

#define PERF_COUNTER_ADD(name, delta) \
    fix_gateway::utils::PerformanceCounters::getInstance().incrementCounter(name, delta)

#define PERF_RATE_RECORD(name) \
    fix_gateway::utils::PerformanceCounters::getInstance().recordRate(name)

#define PERF_RATE_RECORD_N(name, count) \
    fix_gateway::utils::PerformanceCounters::getInstance().recordRate(name, count)

#define PERF_GAUGE_SET(name, value) \
    fix_gateway::utils::PerformanceCounters::getInstance().setGauge(name, value)

    // Predefined metric names for consistency
    namespace metrics
    {
        // Network metrics
        constexpr const char *BYTES_SENT = "network.bytes_sent";
        constexpr const char *BYTES_RECEIVED = "network.bytes_received";
        constexpr const char *MESSAGES_SENT = "network.messages_sent";
        constexpr const char *MESSAGES_RECEIVED = "network.messages_received";
        constexpr const char *CONNECTION_ERRORS = "network.connection_errors";
        constexpr const char *SEND_RATE = "network.send_rate";
        constexpr const char *RECEIVE_RATE = "network.receive_rate";

        // Queue metrics
        constexpr const char *QUEUE_DEPTH = "queue.depth";
        constexpr const char *QUEUE_DROPS = "queue.drops";
        constexpr const char *QUEUE_OVERFLOWS = "queue.overflows";

        // Thread metrics
        constexpr const char *THREAD_CPU_USAGE = "thread.cpu_usage";
        constexpr const char *THREAD_CONTEXT_SWITCHES = "thread.context_switches";

        // System metrics
        constexpr const char *SYSTEM_CPU_USAGE = "system.cpu_usage";
        constexpr const char *SYSTEM_MEMORY_MB = "system.memory_mb";
        constexpr const char *SYSTEM_THREAD_COUNT = "system.thread_count";
    }

} // namespace fix_gateway::utils