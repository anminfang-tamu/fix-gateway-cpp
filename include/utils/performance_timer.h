#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>
#include <limits>

namespace fix_gateway::utils
{
    // High-resolution timestamp type for trading systems
    using Timestamp = std::chrono::time_point<std::chrono::high_resolution_clock>;
    using Duration = std::chrono::nanoseconds;

    /**
     * @brief High-precision timer for measuring latencies in trading systems
     *
     * Provides microsecond-level timing accuracy for critical path performance measurement.
     * Thread-safe and optimized for minimal overhead in hot paths.
     */
    class PerformanceTimer
    {
    public:
        /**
         * @brief Get current high-resolution timestamp
         * @return Timestamp with nanosecond precision
         */
        static Timestamp now() noexcept;

        /**
         * @brief Calculate duration between two timestamps
         * @param start Start timestamp
         * @param end End timestamp
         * @return Duration in nanoseconds
         */
        static Duration duration(const Timestamp &start, const Timestamp &end) noexcept;

        /**
         * @brief Convert duration to microseconds
         * @param duration Duration in nanoseconds
         * @return Duration in microseconds as double
         */
        static double toMicroseconds(const Duration &duration) noexcept;

        /**
         * @brief Convert duration to milliseconds
         * @param duration Duration in nanoseconds
         * @return Duration in milliseconds as double
         */
        static double toMilliseconds(const Duration &duration) noexcept;

        /**
         * @brief Get timestamp as microseconds since epoch
         * @param timestamp Timestamp to convert
         * @return Microseconds since epoch
         */
        static uint64_t toMicrosecondsSinceEpoch(const Timestamp &timestamp) noexcept;

        /**
         * @brief Format timestamp as human-readable string
         * @param timestamp Timestamp to format
         * @return Formatted string "YYYY-MM-DD HH:MM:SS.microseconds"
         */
        static std::string formatTimestamp(const Timestamp &timestamp);
    };

    /**
     * @brief RAII timer for automatic latency measurement
     *
     * Measures time from construction to destruction, useful for scope-based timing.
     * Minimal overhead design for use in critical trading paths.
     */
    class ScopedTimer
    {
    public:
        /**
         * @brief Constructor - starts timing
         * @param name Timer name for identification
         */
        explicit ScopedTimer(const std::string &name);

        /**
         * @brief Constructor with custom callback
         * @param name Timer name
         * @param callback Function called with (name, duration_us) on destruction
         */
        template <typename Callback>
        ScopedTimer(const std::string &name, Callback &&callback)
            : name_(name), start_time_(PerformanceTimer::now()), callback_(std::forward<Callback>(callback))
        {
        }

        /**
         * @brief Destructor - stops timing and reports result
         */
        ~ScopedTimer();

        /**
         * @brief Get elapsed time without stopping timer
         * @return Elapsed microseconds since construction
         */
        double getElapsedMicroseconds() const noexcept;

        // Non-copyable, non-movable for safety
        ScopedTimer(const ScopedTimer &) = delete;
        ScopedTimer &operator=(const ScopedTimer &) = delete;
        ScopedTimer(ScopedTimer &&) = delete;
        ScopedTimer &operator=(ScopedTimer &&) = delete;

    private:
        std::string name_;
        Timestamp start_time_;
        std::function<void(const std::string &, double)> callback_;
    };

    /**
     * @brief Performance statistics collector
     *
     * Collects and aggregates timing statistics for performance analysis.
     * Thread-safe with minimal contention for high-frequency measurements.
     */
    class PerformanceStats
    {
    public:
        struct Stats
        {
            uint64_t count = 0;
            double min_us = std::numeric_limits<double>::max();
            double max_us = 0.0;
            double sum_us = 0.0;
            double sum_squared_us = 0.0;

            double mean() const noexcept { return count > 0 ? sum_us / count : 0.0; }
            double variance() const noexcept;
            double stddev() const noexcept;
        };

        /**
         * @brief Get singleton instance
         * @return Global performance statistics collector
         */
        static PerformanceStats &getInstance();

        /**
         * @brief Record a timing measurement
         * @param name Measurement name/category
         * @param duration_us Duration in microseconds
         */
        void record(const std::string &name, double duration_us);

        /**
         * @brief Get statistics for a named measurement
         * @param name Measurement name
         * @return Statistics structure, empty if name not found
         */
        Stats getStats(const std::string &name) const;

        /**
         * @brief Get all recorded statistics
         * @return Map of name -> statistics
         */
        std::unordered_map<std::string, Stats> getAllStats() const;

        /**
         * @brief Reset all statistics
         */
        void reset();

        /**
         * @brief Print formatted statistics report
         * @param title Report title
         */
        void printReport(const std::string &title = "Performance Report") const;

    private:
        mutable std::mutex stats_mutex_;
        std::unordered_map<std::string, Stats> statistics_;
    };

// Convenience macros for performance timing
#define PERF_TIMER_START(name) \
    auto perf_timer_##name##_start = fix_gateway::utils::PerformanceTimer::now()

#define PERF_TIMER_END(name)                                                                            \
    do                                                                                                  \
    {                                                                                                   \
        auto perf_timer_##name##_end = fix_gateway::utils::PerformanceTimer::now();                     \
        auto perf_timer_##name##_duration = fix_gateway::utils::PerformanceTimer::duration(             \
            perf_timer_##name##_start, perf_timer_##name##_end);                                        \
        fix_gateway::utils::PerformanceStats::getInstance().record(                                     \
            #name, fix_gateway::utils::PerformanceTimer::toMicroseconds(perf_timer_##name##_duration)); \
    } while (0)

#define PERF_SCOPED_TIMER(name) \
    fix_gateway::utils::ScopedTimer perf_scoped_timer(name)

#define PERF_FUNCTION_TIMER() \
    fix_gateway::utils::ScopedTimer perf_function_timer(__FUNCTION__)

} // namespace fix_gateway::utils