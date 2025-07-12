#include "utils/performance_timer.h"
#include "utils/logger.h"
#include <iomanip>
#include <sstream>
#include <cmath>
#include <iostream>

namespace fix_gateway::utils
{
    // PerformanceTimer static methods
    Timestamp PerformanceTimer::now() noexcept
    {
        return std::chrono::high_resolution_clock::now();
    }

    Duration PerformanceTimer::duration(const Timestamp &start, const Timestamp &end) noexcept
    {
        return std::chrono::duration_cast<Duration>(end - start);
    }

    double PerformanceTimer::toMicroseconds(const Duration &duration) noexcept
    {
        return std::chrono::duration<double, std::micro>(duration).count();
    }

    double PerformanceTimer::toMilliseconds(const Duration &duration) noexcept
    {
        return std::chrono::duration<double, std::milli>(duration).count();
    }

    uint64_t PerformanceTimer::toMicrosecondsSinceEpoch(const Timestamp &timestamp) noexcept
    {
        auto duration_since_epoch = timestamp.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(duration_since_epoch).count();
    }

    std::string PerformanceTimer::formatTimestamp(const Timestamp &timestamp)
    {
        // Convert high_resolution_clock timestamp to system_clock for formatting
        auto system_timestamp = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(system_timestamp);

        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                timestamp.time_since_epoch())
                                .count() %
                            1000000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(6) << microseconds;
        return ss.str();
    }

    // ScopedTimer implementation
    ScopedTimer::ScopedTimer(const std::string &name)
        : name_(name), start_time_(PerformanceTimer::now()), callback_(nullptr)
    {
    }

    ScopedTimer::~ScopedTimer()
    {
        auto end_time = PerformanceTimer::now();
        auto duration = PerformanceTimer::duration(start_time_, end_time);
        double duration_us = PerformanceTimer::toMicroseconds(duration);

        if (callback_)
        {
            try
            {
                callback_(name_, duration_us);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Exception in ScopedTimer callback: " + std::string(e.what()));
            }
        }
        else
        {
            // Default behavior: record in global statistics
            PerformanceStats::getInstance().record(name_, duration_us);
        }
    }

    double ScopedTimer::getElapsedMicroseconds() const noexcept
    {
        auto current_time = PerformanceTimer::now();
        auto elapsed = PerformanceTimer::duration(start_time_, current_time);
        return PerformanceTimer::toMicroseconds(elapsed);
    }

    // PerformanceStats implementation
    PerformanceStats &PerformanceStats::getInstance()
    {
        static PerformanceStats instance;
        return instance;
    }

    void PerformanceStats::record(const std::string &name, double duration_us)
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);

        auto &stats = statistics_[name];
        stats.count++;
        stats.min_us = std::min(stats.min_us, duration_us);
        stats.max_us = std::max(stats.max_us, duration_us);
        stats.sum_us += duration_us;
        stats.sum_squared_us += duration_us * duration_us;
    }

    PerformanceStats::Stats PerformanceStats::getStats(const std::string &name) const
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        auto it = statistics_.find(name);
        return (it != statistics_.end()) ? it->second : Stats{};
    }

    std::unordered_map<std::string, PerformanceStats::Stats> PerformanceStats::getAllStats() const
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return statistics_;
    }

    void PerformanceStats::reset()
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.clear();
    }

    double PerformanceStats::Stats::variance() const noexcept
    {
        if (count < 2)
            return 0.0;
        double mean_val = mean();
        return (sum_squared_us - count * mean_val * mean_val) / (count - 1);
    }

    double PerformanceStats::Stats::stddev() const noexcept
    {
        return std::sqrt(variance());
    }

    void PerformanceStats::printReport(const std::string &title) const
    {
        auto all_stats = getAllStats();

        std::cout << "\n"
                  << std::string(80, '=') << "\n";
        std::cout << title << "\n";
        std::cout << std::string(80, '=') << "\n";

        if (all_stats.empty())
        {
            std::cout << "No performance data recorded.\n";
            return;
        }

        // Header
        std::cout << std::left
                  << std::setw(25) << "Operation"
                  << std::setw(10) << "Count"
                  << std::setw(12) << "Min (μs)"
                  << std::setw(12) << "Mean (μs)"
                  << std::setw(12) << "Max (μs)"
                  << std::setw(12) << "StdDev (μs)"
                  << "\n";
        std::cout << std::string(80, '-') << "\n";

        // Data rows
        for (const auto &[name, stats] : all_stats)
        {
            std::cout << std::left << std::fixed << std::setprecision(2)
                      << std::setw(25) << name
                      << std::setw(10) << stats.count
                      << std::setw(12) << stats.min_us
                      << std::setw(12) << stats.mean()
                      << std::setw(12) << stats.max_us
                      << std::setw(12) << stats.stddev()
                      << "\n";
        }

        std::cout << std::string(80, '=') << "\n\n";
    }

} // namespace fix_gateway::utils