#include "utils/logger.h"
#include "utils/performance_timer.h"
#include "utils/performance_counters.h"
#include "network/tcp_connection.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <random>

using namespace fix_gateway;

/**
 * @brief Simple FIX-like message generator for performance testing
 */
class MessageGenerator
{
public:
    MessageGenerator() : rng_(std::random_device{}()) {}

    std::string generateOrder()
    {
        static uint64_t order_id = 1000;
        std::uniform_int_distribution<int> qty_dist(100, 10000);
        std::uniform_real_distribution<double> price_dist(100.0, 1000.0);

        // Simple FIX-like message format (not actual FIX protocol)
        std::string message = "8=FIX.4.4|9=178|35=D|";             // Header
        message += "11=" + std::to_string(order_id++) + "|";       // Order ID
        message += "55=AAPL|";                                     // Symbol
        message += "54=1|";                                        // Side (Buy)
        message += "38=" + std::to_string(qty_dist(rng_)) + "|";   // Quantity
        message += "44=" + std::to_string(price_dist(rng_)) + "|"; // Price
        message += "10=123|";                                      // Checksum (dummy)

        return message;
    }

    std::string generateCancel()
    {
        static uint64_t cancel_id = 2000;
        std::uniform_int_distribution<int> orig_order_dist(1000, 1500);

        std::string message = "8=FIX.4.4|9=145|35=F|"; // Cancel request
        message += "11=" + std::to_string(cancel_id++) + "|";
        message += "41=" + std::to_string(orig_order_dist(rng_)) + "|"; // Original order ID
        message += "55=AAPL|";
        message += "54=1|";
        message += "10=456|";

        return message;
    }

    std::string generateRandomMessage()
    {
        std::uniform_int_distribution<int> type_dist(0, 1);
        return (type_dist(rng_) == 0) ? generateOrder() : generateCancel();
    }

private:
    std::mt19937 rng_;
};

/**
 * @brief Performance baseline test
 */
class BaselineTest
{
public:
    explicit BaselineTest(int num_messages = 10000)
        : num_messages_(num_messages) {}

    void runSendLatencyTest()
    {
        LOG_INFO("=== PHASE 1: Send Latency Baseline Test ===");

        utils::PerformanceStats::getInstance().reset();
        utils::PerformanceCounters::getInstance().reset();

        MessageGenerator generator;

        // Generate messages in advance to avoid allocation during test
        std::vector<std::string> messages;
        messages.reserve(num_messages_);
        for (int i = 0; i < num_messages_; ++i)
        {
            messages.push_back(generator.generateRandomMessage());
        }

        LOG_INFO("Generated " + std::to_string(num_messages_) + " test messages");

        // Measure send timing without actual network (just the send logic)
        auto start_time = utils::PerformanceTimer::now();

        for (const auto &message : messages)
        {
            PERF_SCOPED_TIMER("baseline_send_simulation");

            // Simulate send processing overhead
            simulateMessageProcessing(message);
        }

        auto end_time = utils::PerformanceTimer::now();
        auto total_duration = utils::PerformanceTimer::duration(start_time, end_time);
        double total_ms = utils::PerformanceTimer::toMilliseconds(total_duration);

        // Calculate baseline metrics
        double avg_latency_us = (total_ms * 1000.0) / num_messages_;
        double throughput_msg_per_sec = (num_messages_ * 1000.0) / total_ms;

        LOG_INFO("=== BASELINE RESULTS ===");
        LOG_INFO("Total messages: " + std::to_string(num_messages_));
        LOG_INFO("Total time: " + std::to_string(total_ms) + " ms");
        LOG_INFO("Average latency: " + std::to_string(avg_latency_us) + " μs");
        LOG_INFO("Throughput: " + std::to_string(throughput_msg_per_sec) + " messages/sec");

        // Print detailed performance statistics
        utils::PerformanceStats::getInstance().printReport("Send Simulation Baseline");
    }

    void runMemoryAllocationTest()
    {
        LOG_INFO("=== Memory Allocation Baseline Test ===");

        auto &counters = utils::PerformanceCounters::getInstance();
        counters.reset();

        // Test various allocation patterns
        testStringAllocation();
        testVectorAllocation();
        testMessageCopying();

        counters.printReport("Memory Allocation Baseline");
    }

    void runThreadingOverheadTest()
    {
        LOG_INFO("=== Threading Overhead Baseline Test ===");

        auto &stats = utils::PerformanceStats::getInstance();
        stats.reset();

        // Test mutex contention
        testMutexContention();

        // Test atomic operations
        testAtomicOperations();

        stats.printReport("Threading Overhead Baseline");
    }

private:
    void simulateMessageProcessing(const std::string &message)
    {
        // Simulate typical message processing overhead
        // This represents parsing, validation, and serialization

        PERF_TIMER_START(message_validation);

        // Simulate validation (string operations)
        volatile bool valid = message.length() > 10 && message.find("FIX") != std::string::npos;

        PERF_TIMER_END(message_validation);

        PERF_TIMER_START(message_formatting);

        // Simulate formatting overhead
        std::string formatted = message + "\n";
        volatile size_t len = formatted.length();

        PERF_TIMER_END(message_formatting);

        (void)valid; // Suppress unused variable warning
        (void)len;
    }

    void testStringAllocation()
    {
        LOG_INFO("Testing string allocation patterns...");

        const int iterations = 1000;
        MessageGenerator generator;

        for (int i = 0; i < iterations; ++i)
        {
            PERF_SCOPED_TIMER("string_allocation");

            std::string msg = generator.generateRandomMessage();
            std::string copy = msg;             // Copy construction
            std::string moved = std::move(msg); // Move construction

            volatile size_t total_len = copy.length() + moved.length();
            (void)total_len;
        }
    }

    void testVectorAllocation()
    {
        LOG_INFO("Testing vector allocation patterns...");

        const int iterations = 1000;

        for (int i = 0; i < iterations; ++i)
        {
            PERF_SCOPED_TIMER("vector_allocation");

            std::vector<char> buffer(1024);
            buffer.resize(2048);
            buffer.shrink_to_fit();

            volatile size_t size = buffer.size();
            (void)size;
        }
    }

    void testMessageCopying()
    {
        LOG_INFO("Testing message copying overhead...");

        MessageGenerator generator;
        std::string base_msg = generator.generateRandomMessage();

        const int iterations = 1000;

        for (int i = 0; i < iterations; ++i)
        {
            PERF_SCOPED_TIMER("message_copying");

            std::vector<char> buffer(base_msg.begin(), base_msg.end());
            std::string reconstructed(buffer.begin(), buffer.end());

            volatile bool equal = (reconstructed == base_msg);
            (void)equal;
        }
    }

    void testMutexContention()
    {
        LOG_INFO("Testing mutex contention...");

        std::mutex test_mutex;
        std::atomic<int> counter{0};
        const int iterations = 1000;

        for (int i = 0; i < iterations; ++i)
        {
            PERF_SCOPED_TIMER("mutex_operation");

            std::lock_guard<std::mutex> lock(test_mutex);
            counter++;

            // Simulate some work under lock
            volatile int dummy = counter.load() * 2;
            (void)dummy;
        }
    }

    void testAtomicOperations()
    {
        LOG_INFO("Testing atomic operations...");

        std::atomic<uint64_t> atomic_counter{0};
        const int iterations = 1000;

        for (int i = 0; i < iterations; ++i)
        {
            PERF_SCOPED_TIMER("atomic_operation");

            atomic_counter++;
            atomic_counter.fetch_add(2);
            volatile uint64_t value = atomic_counter.load();
            (void)value;
        }
    }

    int num_messages_;
};

/**
 * @brief System monitoring demonstration
 */
void demonstrateSystemMonitoring()
{
    LOG_INFO("=== System Monitoring Demonstration ===");

    utils::SystemMonitor monitor(std::chrono::seconds(1));
    monitor.start();

    // Let it collect data for a few seconds
    std::this_thread::sleep_for(std::chrono::seconds(3));

    monitor.stop();

    auto &counters = utils::PerformanceCounters::getInstance();
    counters.printReport("System Resource Usage");
}

int main()
{
    LOG_INFO("=== FIX Gateway Phase 1: Performance Baseline Measurement ===");
    LOG_INFO("Starting comprehensive performance instrumentation test...");

    try
    {
        // Run baseline tests
        BaselineTest baseline_test(5000); // 5K messages for baseline

        baseline_test.runSendLatencyTest();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        baseline_test.runMemoryAllocationTest();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        baseline_test.runThreadingOverheadTest();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        demonstrateSystemMonitoring();

        LOG_INFO("=== Phase 1 Baseline Measurement Complete ===");
        LOG_INFO("Next steps:");
        LOG_INFO("1. Review performance metrics above");
        LOG_INFO("2. Document baseline latency numbers");
        LOG_INFO("3. Identify optimization opportunities");
        LOG_INFO("4. Proceed to Phase 2: Async Send Architecture");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception during baseline testing: " + std::string(e.what()));
        return 1;
    }

    return 0;
}