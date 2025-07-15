#include "utils/performance_counters.h"
#include "utils/logger.h"
#include <iostream>
#include <iomanip>
#include <memory>

// Platform-specific includes for system monitoring
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <mach/task.h>
#include <mach/vm_map.h>
#include <sys/sysctl.h>
#elif __linux__
#include <fstream>
#include <unistd.h>
#include <sys/resource.h>
#endif

namespace fix_gateway::utils
{
    // RateTracker implementation
    RateTracker::RateTracker(std::chrono::seconds window_duration)
        : window_duration_(window_duration), last_calculation_(std::chrono::steady_clock::now()), events_in_window_(0), current_rate_(0.0)
    {
    }

    void RateTracker::recordEvent(uint64_t count)
    {
        total_events_.add(count);

        std::lock_guard<std::mutex> lock(rate_mutex_);
        events_in_window_ += count;
    }

    double RateTracker::getCurrentRate()
    {
        std::lock_guard<std::mutex> lock(rate_mutex_);

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_calculation_);

        if (elapsed >= window_duration_)
        {
            // Calculate rate over the elapsed period
            double seconds_elapsed = static_cast<double>(elapsed.count());
            current_rate_ = static_cast<double>(events_in_window_) / seconds_elapsed;

            // Reset for next window
            events_in_window_ = 0;
            last_calculation_ = now;
        }

        return current_rate_;
    }

    void RateTracker::reset()
    {
        std::lock_guard<std::mutex> lock(rate_mutex_);
        total_events_.reset();
        events_in_window_ = 0;
        current_rate_ = 0.0;
        last_calculation_ = std::chrono::steady_clock::now();
    }

    // PerformanceCounters implementation
    PerformanceCounters &PerformanceCounters::getInstance()
    {
        static PerformanceCounters instance;
        return instance;
    }

    AtomicCounter &PerformanceCounters::getCounter(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(counters_mutex_);
        auto it = counters_.find(name);
        if (it == counters_.end())
        {
            auto [inserted_it, success] = counters_.emplace(name, std::make_unique<AtomicCounter>());
            return *inserted_it->second;
        }
        return *it->second;
    }

    void PerformanceCounters::incrementCounter(const std::string &name, uint64_t delta)
    {
        getCounter(name).add(delta);
    }

    uint64_t PerformanceCounters::getCounterValue(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(counters_mutex_);
        auto it = counters_.find(name);
        return (it != counters_.end()) ? it->second->get() : 0;
    }

    RateTracker &PerformanceCounters::getRateTracker(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(rates_mutex_);
        auto it = rate_trackers_.find(name);
        if (it == rate_trackers_.end())
        {
            auto [inserted_it, success] = rate_trackers_.emplace(name, std::make_unique<RateTracker>());
            return *inserted_it->second;
        }
        return *it->second;
    }

    void PerformanceCounters::recordRate(const std::string &name, uint64_t count)
    {
        getRateTracker(name).recordEvent(count);
    }

    double PerformanceCounters::getRateValue(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(rates_mutex_);
        auto it = rate_trackers_.find(name);
        return (it != rate_trackers_.end()) ? it->second->getCurrentRate() : 0.0;
    }

    Gauge &PerformanceCounters::getGauge(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(gauges_mutex_);
        auto it = gauges_.find(name);
        if (it == gauges_.end())
        {
            auto [inserted_it, success] = gauges_.emplace(name, std::make_unique<Gauge>());
            return *inserted_it->second;
        }
        return *it->second;
    }

    void PerformanceCounters::setGauge(const std::string &name, double value)
    {
        getGauge(name).set(value);
    }

    double PerformanceCounters::getGaugeValue(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(gauges_mutex_);
        auto it = gauges_.find(name);
        return (it != gauges_.end()) ? it->second->get() : 0.0;
    }

    void PerformanceCounters::printReport(const std::string &title) const
    {
        std::cout << "\n"
                  << std::string(90, '=') << "\n";
        std::cout << title << "\n";
        std::cout << std::string(90, '=') << "\n";

        // Print counters
        auto counters = getAllCounters();
        if (!counters.empty())
        {
            std::cout << "\nCOUNTERS:\n";
            std::cout << std::string(50, '-') << "\n";
            std::cout << std::left << std::setw(35) << "Name" << std::setw(15) << "Value" << "\n";
            std::cout << std::string(50, '-') << "\n";
            for (const auto &[name, value] : counters)
            {
                std::cout << std::left << std::setw(35) << name << std::setw(15) << value << "\n";
            }
        }

        // Print rates (need to call non-const method)
        auto &self = const_cast<PerformanceCounters &>(*this);
        auto rates = self.getAllRates();
        if (!rates.empty())
        {
            std::cout << "\nRATES (per second):\n";
            std::cout << std::string(50, '-') << "\n";
            std::cout << std::left << std::setw(35) << "Name" << std::setw(15) << "Rate" << "\n";
            std::cout << std::string(50, '-') << "\n";
            for (const auto &[name, rate] : rates)
            {
                std::cout << std::left << std::setw(35) << name
                          << std::setw(15) << std::fixed << std::setprecision(2) << rate << "\n";
            }
        }

        // Print gauges
        auto gauges = getAllGauges();
        if (!gauges.empty())
        {
            std::cout << "\nGAUGES:\n";
            std::cout << std::string(50, '-') << "\n";
            std::cout << std::left << std::setw(35) << "Name" << std::setw(15) << "Value" << "\n";
            std::cout << std::string(50, '-') << "\n";
            for (const auto &[name, value] : gauges)
            {
                std::cout << std::left << std::setw(35) << name
                          << std::setw(15) << std::fixed << std::setprecision(2) << value << "\n";
            }
        }

        std::cout << std::string(90, '=') << "\n\n";
    }

    std::unordered_map<std::string, uint64_t> PerformanceCounters::getAllCounters() const
    {
        std::lock_guard<std::mutex> lock(counters_mutex_);
        std::unordered_map<std::string, uint64_t> result;
        for (const auto &[name, counter] : counters_)
        {
            result[name] = counter->get();
        }
        return result;
    }

    std::unordered_map<std::string, double> PerformanceCounters::getAllRates()
    {
        std::lock_guard<std::mutex> lock(rates_mutex_);
        std::unordered_map<std::string, double> result;
        for (const auto &[name, tracker] : rate_trackers_)
        {
            result[name] = tracker->getCurrentRate();
        }
        return result;
    }

    std::unordered_map<std::string, double> PerformanceCounters::getAllGauges() const
    {
        std::lock_guard<std::mutex> lock(gauges_mutex_);
        std::unordered_map<std::string, double> result;
        for (const auto &[name, gauge] : gauges_)
        {
            result[name] = gauge->get();
        }
        return result;
    }

    void PerformanceCounters::reset()
    {
        {
            std::lock_guard<std::mutex> lock(counters_mutex_);
            for (auto &[name, counter] : counters_)
            {
                counter->reset();
            }
        }
        {
            std::lock_guard<std::mutex> lock(rates_mutex_);
            for (auto &[name, tracker] : rate_trackers_)
            {
                tracker->reset();
            }
        }
        {
            std::lock_guard<std::mutex> lock(gauges_mutex_);
            for (auto &[name, gauge] : gauges_)
            {
                gauge->set(0.0);
            }
        }
    }

    // SystemMonitor implementation
    SystemMonitor::SystemMonitor(std::chrono::seconds update_interval)
        : update_interval_(update_interval), running_(false), cpu_usage_(0.0), memory_usage_mb_(0), thread_count_(0)
    {
    }

    SystemMonitor::~SystemMonitor()
    {
        stop();
    }

    void SystemMonitor::start()
    {
        if (running_)
        {
            LOG_WARN("SystemMonitor already running");
            return;
        }

        running_ = true;
        monitor_thread_ = std::thread(&SystemMonitor::monitorLoop, this);
        LOG_INFO("SystemMonitor started");
    }

    void SystemMonitor::stop()
    {
        if (!running_)
        {
            return;
        }

        running_ = false;
        if (monitor_thread_.joinable())
        {
            monitor_thread_.join();
        }
        LOG_INFO("SystemMonitor stopped");
    }

    double SystemMonitor::getCpuUsage() const
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return cpu_usage_;
    }

    uint64_t SystemMonitor::getMemoryUsageMB() const
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return memory_usage_mb_;
    }

    uint32_t SystemMonitor::getThreadCount() const
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return thread_count_;
    }

    void SystemMonitor::monitorLoop()
    {
        while (running_)
        {
            updateCpuUsage();
            updateMemoryUsage();
            updateThreadCount();

            // Update global gauges
            auto &counters = PerformanceCounters::getInstance();
            counters.setGauge(metrics::SYSTEM_CPU_USAGE, getCpuUsage());
            counters.setGauge(metrics::SYSTEM_MEMORY_MB, static_cast<double>(getMemoryUsageMB()));
            counters.setGauge(metrics::SYSTEM_THREAD_COUNT, static_cast<double>(getThreadCount()));

            std::this_thread::sleep_for(update_interval_);
        }
    }

    void SystemMonitor::updateCpuUsage()
    {
#ifdef __APPLE__
        host_cpu_load_info_data_t cpu_info;
        mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;

        if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                            reinterpret_cast<host_info_t>(&cpu_info), &count) == KERN_SUCCESS)
        {
            static uint64_t prev_idle = 0, prev_total = 0;

            uint64_t idle = cpu_info.cpu_ticks[CPU_STATE_IDLE];
            uint64_t total = 0;
            for (int i = 0; i < CPU_STATE_MAX; i++)
            {
                total += cpu_info.cpu_ticks[i];
            }

            if (prev_total > 0)
            {
                uint64_t idle_diff = idle - prev_idle;
                uint64_t total_diff = total - prev_total;

                if (total_diff > 0)
                {
                    double usage = 100.0 * (1.0 - static_cast<double>(idle_diff) / total_diff);
                    std::lock_guard<std::mutex> lock(metrics_mutex_);
                    cpu_usage_ = std::max(0.0, std::min(100.0, usage));
                }
            }

            prev_idle = idle;
            prev_total = total;
        }
#elif __linux__
        // Linux implementation using /proc/stat
        std::ifstream stat_file("/proc/stat");
        if (stat_file.is_open())
        {
            std::string line;
            std::getline(stat_file, line);

            // Parse first line: cpu user nice system idle iowait irq softirq steal
            uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
            if (sscanf(line.c_str(), "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8)
            {
                static uint64_t prev_idle = 0, prev_total = 0;

                uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;

                if (prev_total > 0)
                {
                    uint64_t idle_diff = idle - prev_idle;
                    uint64_t total_diff = total - prev_total;

                    if (total_diff > 0)
                    {
                        double usage = 100.0 * (1.0 - static_cast<double>(idle_diff) / total_diff);
                        std::lock_guard<std::mutex> lock(metrics_mutex_);
                        cpu_usage_ = std::max(0.0, std::min(100.0, usage));
                    }
                }

                prev_idle = idle;
                prev_total = total;
            }
        }
#endif
    }

    void SystemMonitor::updateMemoryUsage()
    {
#ifdef __APPLE__
        task_vm_info_data_t vm_info;
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;

        if (task_info(mach_task_self(), TASK_VM_INFO,
                      reinterpret_cast<task_info_t>(&vm_info), &count) == KERN_SUCCESS)
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            memory_usage_mb_ = vm_info.phys_footprint / (1024 * 1024);
        }
#elif __linux__
        std::ifstream status_file("/proc/self/status");
        std::string line;

        while (std::getline(status_file, line))
        {
            if (line.substr(0, 6) == "VmRSS:")
            {
                uint64_t rss_kb;
                if (sscanf(line.c_str(), "VmRSS: %lu kB", &rss_kb) == 1)
                {
                    std::lock_guard<std::mutex> lock(metrics_mutex_);
                    memory_usage_mb_ = rss_kb / 1024;
                }
                break;
            }
        }
#endif
    }

    void SystemMonitor::updateThreadCount()
    {
#ifdef __APPLE__
        task_info_data_t task_info_data;
        mach_msg_type_number_t count = TASK_INFO_MAX;

        if (task_info(mach_task_self(), TASK_BASIC_INFO,
                      reinterpret_cast<task_info_t>(&task_info_data), &count) == KERN_SUCCESS)
        {
            task_basic_info_t basic_info = reinterpret_cast<task_basic_info_t>(&task_info_data);
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            thread_count_ = basic_info->user_time.seconds + basic_info->system_time.seconds; // Approximation
        }
#elif __linux__
        std::ifstream status_file("/proc/self/status");
        std::string line;

        while (std::getline(status_file, line))
        {
            if (line.substr(0, 8) == "Threads:")
            {
                uint32_t threads;
                if (sscanf(line.c_str(), "Threads: %u", &threads) == 1)
                {
                    std::lock_guard<std::mutex> lock(metrics_mutex_);
                    thread_count_ = threads;
                }
                break;
            }
        }
#endif
    }

} // namespace fix_gateway::utils