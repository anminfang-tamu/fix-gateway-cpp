#include "protocol/stream_fix_parser.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"
#include "utils/logger.h"
#include "utils/performance_timer.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <memory>
#include <random>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <unistd.h>
#include <sys/resource.h>

using namespace fix_gateway::protocol;
using namespace fix_gateway::common;
using namespace fix_gateway::utils;

class StreamFixParserPerformanceTest
{
private:
    MessagePool<FixMessage> *message_pool_;
    std::unique_ptr<StreamFixParser> parser_;
    std::vector<std::string> test_messages_;
    std::mt19937 rng_;
    std::string results_file_;

    struct PerformanceMetrics
    {
        uint64_t total_messages = 0;
        uint64_t total_bytes = 0;
        uint64_t total_time_ns = 0;
        uint64_t min_parse_time_ns = UINT64_MAX;
        uint64_t max_parse_time_ns = 0;
        uint64_t parse_errors = 0;
        uint64_t allocation_failures = 0;
        std::vector<uint64_t> parse_times_ns;
        double throughput_mps = 0.0;  // Messages per second
        double throughput_mbps = 0.0; // Megabytes per second
        double avg_latency_ns = 0.0;
        double p50_latency_ns = 0.0;
        double p95_latency_ns = 0.0;
        double p99_latency_ns = 0.0;
        size_t memory_peak_kb = 0;
        size_t memory_current_kb = 0;
    };

public:
    StreamFixParserPerformanceTest() : message_pool_(nullptr), rng_(std::random_device{}())
    {
        // Create timestamped results file
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);

        char timestamp[100];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
        results_file_ = "stream_fix_parser_performance_" + std::string(timestamp) + ".txt";
    }

    ~StreamFixParserPerformanceTest()
    {
        cleanup();
    }

    bool setup(size_t pool_size = 10000)
    {
        try
        {
            // Create larger message pool for performance testing
            message_pool_ = new MessagePool<FixMessage>(pool_size, "perf_test_pool");
            message_pool_->prewarm();

            // Create parser with optimized settings
            parser_ = std::make_unique<StreamFixParser>(message_pool_);
            parser_->setValidateChecksum(true); // Test with validation enabled
            parser_->setStrictValidation(true);
            parser_->setMaxConsecutiveErrors(100);
            parser_->setErrorRecoveryEnabled(true);

            // Pre-generate test messages
            generateTestMessages();

            std::cout << "✅ Performance test setup completed" << std::endl;
            std::cout << "   Message pool size: " << pool_size << std::endl;
            std::cout << "   Test messages generated: " << test_messages_.size() << std::endl;
            std::cout << "   Results will be saved to: " << results_file_ << std::endl;

            return true;
        }
        catch (const std::exception &e)
        {
            std::cout << "❌ Setup failed: " << e.what() << std::endl;
            return false;
        }
    }

    void cleanup()
    {
        parser_.reset();
        if (message_pool_)
        {
            delete message_pool_;
            message_pool_ = nullptr;
        }
    }

    // =================================================================
    // TEST MESSAGE GENERATION
    // =================================================================

    void generateTestMessages()
    {
        test_messages_.clear();

        // Generate various message types and sizes
        std::vector<std::string> msg_types = {"D", "8", "G", "9", "A", "0", "1", "2", "3", "4", "5"};
        std::vector<std::string> symbols = {"AAPL", "GOOGL", "MSFT", "AMZN", "TSLA", "META", "NVDA", "NFLX"};
        std::vector<std::string> sides = {"1", "2"};
        std::vector<std::string> ord_types = {"1", "2", "3", "4"};

        // Generate 1000 different messages for variety
        for (int i = 0; i < 1000; ++i)
        {
            std::string msg_type = msg_types[rng_() % msg_types.size()];
            std::string symbol = symbols[rng_() % symbols.size()];
            std::string side = sides[rng_() % sides.size()];
            std::string ord_type = ord_types[rng_() % ord_types.size()];

            test_messages_.push_back(createTestMessage(msg_type, symbol, side, ord_type, i));
        }

        // Add some larger messages with extra fields
        for (int i = 0; i < 100; ++i)
        {
            test_messages_.push_back(createLargeTestMessage(i));
        }

        std::cout << "Generated " << test_messages_.size() << " test messages" << std::endl;
    }

    std::string createTestMessage(const std::string &msg_type, const std::string &symbol,
                                  const std::string &side, const std::string &ord_type, int seq_num)
    {
        std::string message = "8=FIX.4.4\x01";

        // Build body
        std::string body = "35=" + msg_type + "\x01";
        body += "49=SENDER" + std::to_string(seq_num % 10) + "\x01";
        body += "56=TARGET" + std::to_string(seq_num % 5) + "\x01";
        body += "52=20231201-12:00:00." + std::to_string(seq_num % 1000) + "\x01";
        body += "11=ORDER" + std::to_string(seq_num) + "\x01";
        body += "55=" + symbol + "\x01";
        body += "54=" + side + "\x01";
        body += "38=" + std::to_string(100 + (seq_num % 1000)) + "\x01";
        body += "44=" + std::to_string(100.0 + (seq_num % 500) * 0.25) + "\x01";
        body += "40=" + ord_type + "\x01";
        body += "59=" + std::to_string(seq_num % 4) + "\x01";
        body += "60=20231201-12:00:00\x01";

        // Add BodyLength
        message += "9=" + std::to_string(body.length()) + "\x01";
        message += body;

        // Calculate and add checksum
        uint8_t checksum = 0;
        for (char c : message)
        {
            checksum += static_cast<uint8_t>(c);
        }
        checksum %= 256;

        char checksum_str[4];
        snprintf(checksum_str, sizeof(checksum_str), "%03d", checksum);
        message += "10=" + std::string(checksum_str) + "\x01";

        return message;
    }

    std::string createLargeTestMessage(int seq_num)
    {
        std::string message = "8=FIX.4.4\x01";

        // Build larger body with many optional fields
        std::string body = "35=D\x01";
        body += "49=LARGE_SENDER_" + std::to_string(seq_num) + "\x01";
        body += "56=LARGE_TARGET_" + std::to_string(seq_num) + "\x01";
        body += "52=20231201-12:00:00.000\x01";
        body += "11=LARGE_ORDER_" + std::to_string(seq_num) + "\x01";
        body += "55=LARGE_SYMBOL_" + std::to_string(seq_num) + "\x01";
        body += "54=1\x01";
        body += "38=1000\x01";
        body += "44=250.75\x01";
        body += "40=2\x01";
        body += "59=0\x01";
        body += "60=20231201-12:00:00\x01";

        // Add many optional fields
        for (int i = 0; i < 20; ++i)
        {
            body += std::to_string(5000 + i) + "=OPTIONAL_FIELD_" + std::to_string(i) + "_" + std::to_string(seq_num) + "\x01";
        }

        // Add repeating group
        body += "453=3\x01"; // Number of parties
        for (int i = 0; i < 3; ++i)
        {
            body += "448=PARTY_" + std::to_string(i) + "_" + std::to_string(seq_num) + "\x01";
            body += "447=" + std::to_string(i + 1) + "\x01";
            body += "452=" + std::to_string(10 + i) + "\x01";
        }

        // Add BodyLength
        message += "9=" + std::to_string(body.length()) + "\x01";
        message += body;

        // Calculate and add checksum
        uint8_t checksum = 0;
        for (char c : message)
        {
            checksum += static_cast<uint8_t>(c);
        }
        checksum %= 256;

        char checksum_str[4];
        snprintf(checksum_str, sizeof(checksum_str), "%03d", checksum);
        message += "10=" + std::string(checksum_str) + "\x01";

        return message;
    }

    // =================================================================
    // MEMORY MONITORING
    // =================================================================

    size_t getCurrentMemoryUsageKB()
    {
        std::ifstream file("/proc/self/status");
        std::string line;
        while (std::getline(file, line))
        {
            if (line.substr(0, 6) == "VmRSS:")
            {
                std::istringstream iss(line);
                std::string name, value, unit;
                iss >> name >> value >> unit;
                return std::stoull(value);
            }
        }
        return 0;
    }

    size_t getPeakMemoryUsageKB()
    {
        std::ifstream file("/proc/self/status");
        std::string line;
        while (std::getline(file, line))
        {
            if (line.substr(0, 7) == "VmHWM:")
            {
                std::istringstream iss(line);
                std::string name, value, unit;
                iss >> name >> value >> unit;
                return std::stoull(value);
            }
        }
        return 0;
    }

    // =================================================================
    // PERFORMANCE TESTS
    // =================================================================

    PerformanceMetrics testSingleThreadedThroughput(size_t num_messages = 100000, bool verbose = true)
    {
        if (verbose)
            std::cout << "\n--- Single-Threaded Throughput Test ---" << std::endl;

        PerformanceMetrics metrics;
        parser_->reset();
        parser_->resetStats();

        size_t memory_start = getCurrentMemoryUsageKB();
        auto start_time = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < num_messages; ++i)
        {
            const std::string &msg = test_messages_[i % test_messages_.size()];

            auto parse_start = std::chrono::high_resolution_clock::now();
            auto result = parser_->parse(msg.data(), msg.length());
            auto parse_end = std::chrono::high_resolution_clock::now();

            uint64_t parse_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(parse_end - parse_start).count();

            metrics.parse_times_ns.push_back(parse_time_ns);
            metrics.total_time_ns += parse_time_ns;
            metrics.total_bytes += msg.length();

            metrics.min_parse_time_ns = std::min(metrics.min_parse_time_ns, parse_time_ns);
            metrics.max_parse_time_ns = std::max(metrics.max_parse_time_ns, parse_time_ns);

            if (result.status == StreamFixParser::ParseStatus::Success)
            {
                metrics.total_messages++;
                if (result.parsed_message)
                {
                    message_pool_->deallocate(result.parsed_message);
                }
            }
            else
            {
                metrics.parse_errors++;
                if (result.status == StreamFixParser::ParseStatus::AllocationFailed)
                {
                    metrics.allocation_failures++;
                }
            }

            parser_->reset();

            // Progress indicator
            if (verbose && i > 0 && i % 10000 == 0)
            {
                std::cout << "  Processed " << i << " messages..." << std::endl;
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        uint64_t total_wall_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

        metrics.memory_current_kb = getCurrentMemoryUsageKB();
        metrics.memory_peak_kb = getPeakMemoryUsageKB();

        // Calculate derived metrics
        if (total_wall_time_ns > 0)
        {
            metrics.throughput_mps = static_cast<double>(metrics.total_messages) / (total_wall_time_ns / 1e9);
            metrics.throughput_mbps = static_cast<double>(metrics.total_bytes) / (1024.0 * 1024.0) / (total_wall_time_ns / 1e9);
        }

        if (metrics.total_messages > 0)
        {
            metrics.avg_latency_ns = static_cast<double>(metrics.total_time_ns) / metrics.total_messages;
        }

        // Calculate percentiles
        if (!metrics.parse_times_ns.empty())
        {
            std::sort(metrics.parse_times_ns.begin(), metrics.parse_times_ns.end());
            size_t size = metrics.parse_times_ns.size();
            metrics.p50_latency_ns = metrics.parse_times_ns[size * 50 / 100];
            metrics.p95_latency_ns = metrics.parse_times_ns[size * 95 / 100];
            metrics.p99_latency_ns = metrics.parse_times_ns[size * 99 / 100];
        }

        if (verbose)
        {
            printMetrics(metrics, "Single-Threaded Throughput");
        }

        return metrics;
    }

    PerformanceMetrics testPartialMessagePerformance(size_t num_messages = 10000, bool verbose = true)
    {
        if (verbose)
            std::cout << "\n--- Partial Message Performance Test ---" << std::endl;

        PerformanceMetrics metrics;
        parser_->reset();
        parser_->resetStats();

        auto start_time = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < num_messages; ++i)
        {
            const std::string &complete_msg = test_messages_[i % test_messages_.size()];

            // Split message into random-sized chunks
            std::uniform_int_distribution<size_t> chunk_dist(4, 20);
            size_t pos = 0;
            bool message_complete = false;

            auto msg_parse_start = std::chrono::high_resolution_clock::now();

            while (pos < complete_msg.length() && !message_complete)
            {
                size_t chunk_size = std::min(chunk_dist(rng_), complete_msg.length() - pos);

                auto result = parser_->parse(complete_msg.data() + pos, chunk_size);

                if (result.status == StreamFixParser::ParseStatus::Success)
                {
                    message_complete = true;
                    metrics.total_messages++;
                    if (result.parsed_message)
                    {
                        message_pool_->deallocate(result.parsed_message);
                    }
                }
                else if (result.status == StreamFixParser::ParseStatus::NeedMoreData)
                {
                    // Continue with next chunk
                }
                else
                {
                    metrics.parse_errors++;
                    break;
                }

                pos += chunk_size;
            }

            auto msg_parse_end = std::chrono::high_resolution_clock::now();
            uint64_t parse_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(msg_parse_end - msg_parse_start).count();

            metrics.parse_times_ns.push_back(parse_time_ns);
            metrics.total_time_ns += parse_time_ns;
            metrics.total_bytes += complete_msg.length();

            metrics.min_parse_time_ns = std::min(metrics.min_parse_time_ns, parse_time_ns);
            metrics.max_parse_time_ns = std::max(metrics.max_parse_time_ns, parse_time_ns);

            parser_->reset();

            if (verbose && i > 0 && i % 1000 == 0)
            {
                std::cout << "  Processed " << i << " partial messages..." << std::endl;
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        uint64_t total_wall_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

        // Calculate derived metrics
        if (total_wall_time_ns > 0)
        {
            metrics.throughput_mps = static_cast<double>(metrics.total_messages) / (total_wall_time_ns / 1e9);
            metrics.throughput_mbps = static_cast<double>(metrics.total_bytes) / (1024.0 * 1024.0) / (total_wall_time_ns / 1e9);
        }

        if (metrics.total_messages > 0)
        {
            metrics.avg_latency_ns = static_cast<double>(metrics.total_time_ns) / metrics.total_messages;
        }

        // Calculate percentiles
        if (!metrics.parse_times_ns.empty())
        {
            std::sort(metrics.parse_times_ns.begin(), metrics.parse_times_ns.end());
            size_t size = metrics.parse_times_ns.size();
            metrics.p50_latency_ns = metrics.parse_times_ns[size * 50 / 100];
            metrics.p95_latency_ns = metrics.parse_times_ns[size * 95 / 100];
            metrics.p99_latency_ns = metrics.parse_times_ns[size * 99 / 100];
        }

        if (verbose)
        {
            printMetrics(metrics, "Partial Message Performance");
        }

        return metrics;
    }

    struct ThreadTestResult
    {
        std::atomic<uint64_t> messages_processed{0};
        std::atomic<uint64_t> parse_errors{0};
        std::atomic<uint64_t> total_time_ns{0};
        std::vector<uint64_t> parse_times_ns;
    };

    PerformanceMetrics testMultiThreadedPerformance(size_t num_threads = 4, size_t messages_per_thread = 25000, bool verbose = true)
    {
        if (verbose)
            std::cout << "\n--- Multi-Threaded Performance Test (" << num_threads << " threads) ---" << std::endl;

        PerformanceMetrics combined_metrics;
        std::vector<std::unique_ptr<MessagePool<FixMessage>>> thread_pools;
        std::vector<std::unique_ptr<StreamFixParser>> thread_parsers;
        std::vector<ThreadTestResult> thread_results(num_threads);
        std::vector<std::thread> threads;

        // Create separate pools and parsers for each thread
        for (size_t i = 0; i < num_threads; ++i)
        {
            thread_pools.push_back(std::make_unique<MessagePool<FixMessage>>(10000, "thread_pool_" + std::to_string(i)));
            thread_pools[i]->prewarm();
            thread_parsers.push_back(std::make_unique<StreamFixParser>(thread_pools[i].get()));
            thread_parsers[i]->setValidateChecksum(true);
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        // Launch threads
        for (size_t t = 0; t < num_threads; ++t)
        {
            threads.emplace_back([this, t, &thread_results, &thread_parsers, &thread_pools, messages_per_thread]()
                                 {
                ThreadTestResult &result = thread_results[t];
                StreamFixParser *parser = thread_parsers[t].get();
                
                for (size_t i = 0; i < messages_per_thread; ++i)
                {
                    const std::string &msg = test_messages_[(t * messages_per_thread + i) % test_messages_.size()];
                    
                    auto parse_start = std::chrono::high_resolution_clock::now();
                    auto parse_result = parser->parse(msg.data(), msg.length());
                    auto parse_end = std::chrono::high_resolution_clock::now();
                    
                    uint64_t parse_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(parse_end - parse_start).count();
                    result.parse_times_ns.push_back(parse_time_ns);
                    result.total_time_ns += parse_time_ns;
                    
                    if (parse_result.status == StreamFixParser::ParseStatus::Success)
                    {
                        result.messages_processed++;
                        if (parse_result.parsed_message)
                        {
                            thread_pools[t]->deallocate(parse_result.parsed_message);
                        }
                    }
                    else
                    {
                        result.parse_errors++;
                    }
                    
                    parser->reset();
                } });
        }

        // Wait for all threads to complete
        for (auto &thread : threads)
        {
            thread.join();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        uint64_t total_wall_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

        // Combine results from all threads
        combined_metrics.total_messages = 0;
        combined_metrics.parse_errors = 0;
        combined_metrics.total_time_ns = 0;
        combined_metrics.total_bytes = 0;
        combined_metrics.min_parse_time_ns = UINT64_MAX;
        combined_metrics.max_parse_time_ns = 0;

        for (size_t t = 0; t < num_threads; ++t)
        {
            const ThreadTestResult &result = thread_results[t];
            combined_metrics.total_messages += result.messages_processed;
            combined_metrics.parse_errors += result.parse_errors;
            combined_metrics.total_time_ns += result.total_time_ns;
            combined_metrics.total_bytes += messages_per_thread * 200; // Approximate average message size

            // Combine parse times for percentile calculations
            combined_metrics.parse_times_ns.insert(combined_metrics.parse_times_ns.end(),
                                                   result.parse_times_ns.begin(),
                                                   result.parse_times_ns.end());

            if (!result.parse_times_ns.empty())
            {
                auto min_it = std::min_element(result.parse_times_ns.begin(), result.parse_times_ns.end());
                auto max_it = std::max_element(result.parse_times_ns.begin(), result.parse_times_ns.end());
                combined_metrics.min_parse_time_ns = std::min(combined_metrics.min_parse_time_ns, *min_it);
                combined_metrics.max_parse_time_ns = std::max(combined_metrics.max_parse_time_ns, *max_it);
            }
        }

        // Calculate derived metrics
        if (total_wall_time_ns > 0)
        {
            combined_metrics.throughput_mps = static_cast<double>(combined_metrics.total_messages) / (total_wall_time_ns / 1e9);
            combined_metrics.throughput_mbps = static_cast<double>(combined_metrics.total_bytes) / (1024.0 * 1024.0) / (total_wall_time_ns / 1e9);
        }

        if (combined_metrics.total_messages > 0)
        {
            combined_metrics.avg_latency_ns = static_cast<double>(combined_metrics.total_time_ns) / combined_metrics.total_messages;
        }

        // Calculate percentiles
        if (!combined_metrics.parse_times_ns.empty())
        {
            std::sort(combined_metrics.parse_times_ns.begin(), combined_metrics.parse_times_ns.end());
            size_t size = combined_metrics.parse_times_ns.size();
            combined_metrics.p50_latency_ns = combined_metrics.parse_times_ns[size * 50 / 100];
            combined_metrics.p95_latency_ns = combined_metrics.parse_times_ns[size * 95 / 100];
            combined_metrics.p99_latency_ns = combined_metrics.parse_times_ns[size * 99 / 100];
        }

        if (verbose)
        {
            printMetrics(combined_metrics, "Multi-Threaded Performance (" + std::to_string(num_threads) + " threads)");
        }

        return combined_metrics;
    }

    PerformanceMetrics testSustainedLoad(std::chrono::seconds duration = std::chrono::seconds(30), bool verbose = true)
    {
        if (verbose)
            std::cout << "\n--- Sustained Load Test (" << duration.count() << " seconds) ---" << std::endl;

        PerformanceMetrics metrics;
        parser_->reset();
        parser_->resetStats();

        auto start_time = std::chrono::high_resolution_clock::now();
        auto end_time = start_time + duration;

        size_t message_index = 0;
        uint32_t report_interval = 0;

        while (std::chrono::high_resolution_clock::now() < end_time)
        {
            const std::string &msg = test_messages_[message_index % test_messages_.size()];

            auto parse_start = std::chrono::high_resolution_clock::now();
            auto result = parser_->parse(msg.data(), msg.length());
            auto parse_end = std::chrono::high_resolution_clock::now();

            uint64_t parse_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(parse_end - parse_start).count();

            metrics.parse_times_ns.push_back(parse_time_ns);
            metrics.total_time_ns += parse_time_ns;
            metrics.total_bytes += msg.length();

            metrics.min_parse_time_ns = std::min(metrics.min_parse_time_ns, parse_time_ns);
            metrics.max_parse_time_ns = std::max(metrics.max_parse_time_ns, parse_time_ns);

            if (result.status == StreamFixParser::ParseStatus::Success)
            {
                metrics.total_messages++;
                if (result.parsed_message)
                {
                    message_pool_->deallocate(result.parsed_message);
                }
            }
            else
            {
                metrics.parse_errors++;
            }

            parser_->reset();
            message_index++;

            // Progress reporting every 10 seconds
            if (verbose && ++report_interval % 50000 == 0)
            {
                auto current_time = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time);
                std::cout << "  " << elapsed.count() << "s: " << metrics.total_messages << " messages processed" << std::endl;
            }
        }

        auto actual_end_time = std::chrono::high_resolution_clock::now();
        uint64_t total_wall_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(actual_end_time - start_time).count();

        // Calculate derived metrics
        if (total_wall_time_ns > 0)
        {
            metrics.throughput_mps = static_cast<double>(metrics.total_messages) / (total_wall_time_ns / 1e9);
            metrics.throughput_mbps = static_cast<double>(metrics.total_bytes) / (1024.0 * 1024.0) / (total_wall_time_ns / 1e9);
        }

        if (metrics.total_messages > 0)
        {
            metrics.avg_latency_ns = static_cast<double>(metrics.total_time_ns) / metrics.total_messages;
        }

        // Calculate percentiles
        if (!metrics.parse_times_ns.empty())
        {
            std::sort(metrics.parse_times_ns.begin(), metrics.parse_times_ns.end());
            size_t size = metrics.parse_times_ns.size();
            metrics.p50_latency_ns = metrics.parse_times_ns[size * 50 / 100];
            metrics.p95_latency_ns = metrics.parse_times_ns[size * 95 / 100];
            metrics.p99_latency_ns = metrics.parse_times_ns[size * 99 / 100];
        }

        if (verbose)
        {
            printMetrics(metrics, "Sustained Load Test");
        }

        return metrics;
    }

    // =================================================================
    // RESULTS REPORTING
    // =================================================================

    void printMetrics(const PerformanceMetrics &metrics, const std::string &test_name)
    {
        std::cout << "\n📊 " << test_name << " Results:" << std::endl;
        std::cout << "  Messages processed:     " << std::setw(10) << metrics.total_messages << std::endl;
        std::cout << "  Parse errors:           " << std::setw(10) << metrics.parse_errors << std::endl;
        std::cout << "  Allocation failures:    " << std::setw(10) << metrics.allocation_failures << std::endl;
        std::cout << "  Total bytes processed:  " << std::setw(10) << metrics.total_bytes << " bytes" << std::endl;
        std::cout << "  Throughput:             " << std::setw(10) << std::fixed << std::setprecision(2) << metrics.throughput_mps << " msgs/sec" << std::endl;
        std::cout << "  Throughput:             " << std::setw(10) << std::fixed << std::setprecision(2) << metrics.throughput_mbps << " MB/sec" << std::endl;
        std::cout << "  Average latency:        " << std::setw(10) << std::fixed << std::setprecision(0) << metrics.avg_latency_ns << " ns" << std::endl;
        std::cout << "  Min latency:            " << std::setw(10) << metrics.min_parse_time_ns << " ns" << std::endl;
        std::cout << "  Max latency:            " << std::setw(10) << metrics.max_parse_time_ns << " ns" << std::endl;
        std::cout << "  P50 latency:            " << std::setw(10) << std::fixed << std::setprecision(0) << metrics.p50_latency_ns << " ns" << std::endl;
        std::cout << "  P95 latency:            " << std::setw(10) << std::fixed << std::setprecision(0) << metrics.p95_latency_ns << " ns" << std::endl;
        std::cout << "  P99 latency:            " << std::setw(10) << std::fixed << std::setprecision(0) << metrics.p99_latency_ns << " ns" << std::endl;

        if (metrics.memory_current_kb > 0)
        {
            std::cout << "  Current memory:         " << std::setw(10) << metrics.memory_current_kb << " KB" << std::endl;
        }
        if (metrics.memory_peak_kb > 0)
        {
            std::cout << "  Peak memory:            " << std::setw(10) << metrics.memory_peak_kb << " KB" << std::endl;
        }
    }

    void saveResultsToFile(const std::vector<std::pair<std::string, PerformanceMetrics>> &all_results)
    {
        std::ofstream file(results_file_);
        if (!file.is_open())
        {
            std::cout << "❌ Failed to open results file: " << results_file_ << std::endl;
            return;
        }

        // System information
        file << "StreamFixParser Performance Test Results\n";
        file << "========================================\n\n";

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        file << "Test Date: " << std::ctime(&time_t);

        file << "System Information:\n";
        file << "  CPU cores: " << std::thread::hardware_concurrency() << "\n";
        file << "  Page size: " << getpagesize() << " bytes\n";
        file << "  Message pool size: " << (message_pool_ ? message_pool_->capacity() : 0) << "\n";
        file << "  Test messages: " << test_messages_.size() << "\n\n";

        // Test results
        for (const auto &[test_name, metrics] : all_results)
        {
            file << test_name << ":\n";
            file << "  Messages processed: " << metrics.total_messages << "\n";
            file << "  Parse errors: " << metrics.parse_errors << "\n";
            file << "  Allocation failures: " << metrics.allocation_failures << "\n";
            file << "  Total bytes: " << metrics.total_bytes << "\n";
            file << "  Throughput (msgs/sec): " << std::fixed << std::setprecision(2) << metrics.throughput_mps << "\n";
            file << "  Throughput (MB/sec): " << std::fixed << std::setprecision(2) << metrics.throughput_mbps << "\n";
            file << "  Average latency (ns): " << std::fixed << std::setprecision(0) << metrics.avg_latency_ns << "\n";
            file << "  Min latency (ns): " << metrics.min_parse_time_ns << "\n";
            file << "  Max latency (ns): " << metrics.max_parse_time_ns << "\n";
            file << "  P50 latency (ns): " << std::fixed << std::setprecision(0) << metrics.p50_latency_ns << "\n";
            file << "  P95 latency (ns): " << std::fixed << std::setprecision(0) << metrics.p95_latency_ns << "\n";
            file << "  P99 latency (ns): " << std::fixed << std::setprecision(0) << metrics.p99_latency_ns << "\n";

            if (metrics.memory_current_kb > 0)
            {
                file << "  Memory current (KB): " << metrics.memory_current_kb << "\n";
            }
            if (metrics.memory_peak_kb > 0)
            {
                file << "  Memory peak (KB): " << metrics.memory_peak_kb << "\n";
            }
            file << "\n";
        }

        file.close();
        std::cout << "📄 Results saved to: " << results_file_ << std::endl;
    }

    // =================================================================
    // MAIN TEST RUNNER
    // =================================================================

    bool runAllPerformanceTests()
    {
        std::cout << "=== StreamFixParser Performance Test Suite ===" << std::endl;
        std::cout << "Testing high-performance FIX message parsing on Linux" << std::endl;
        std::cout << "=====================================================" << std::endl;

        if (!setup())
        {
            return false;
        }

        std::vector<std::pair<std::string, PerformanceMetrics>> all_results;

        try
        {
            // Run all performance tests
            all_results.emplace_back("Single-Threaded Throughput", testSingleThreadedThroughput(100000));
            all_results.emplace_back("Partial Message Performance", testPartialMessagePerformance(10000));
            all_results.emplace_back("Multi-Threaded Performance (2 threads)", testMultiThreadedPerformance(2, 25000));
            all_results.emplace_back("Multi-Threaded Performance (4 threads)", testMultiThreadedPerformance(4, 25000));
            all_results.emplace_back("Multi-Threaded Performance (8 threads)", testMultiThreadedPerformance(8, 12500));
            all_results.emplace_back("Sustained Load Test", testSustainedLoad(std::chrono::seconds(30)));

            // Save results to file
            saveResultsToFile(all_results);

            std::cout << "\n=====================================================" << std::endl;
            std::cout << "🎉 ALL PERFORMANCE TESTS COMPLETED SUCCESSFULLY!" << std::endl;
            std::cout << "📊 Check the results file for detailed metrics" << std::endl;
            std::cout << "=====================================================" << std::endl;

            return true;
        }
        catch (const std::exception &e)
        {
            std::cout << "❌ Performance test suite failed with exception: " << e.what() << std::endl;
            return false;
        }
    }
};

// =================================================================
// MAIN FUNCTION
// =================================================================

int main(int argc, char *argv[])
{
    // Set logging to minimal for performance testing
    fix_gateway::utils::Logger::getInstance().setLogLevel(fix_gateway::utils::LogLevel::INFO);
    fix_gateway::utils::Logger::getInstance().enableConsoleOutput(false);

    std::cout << "StreamFixParser Performance Testing Suite" << std::endl;
    std::cout << "CPU cores available: " << std::thread::hardware_concurrency() << std::endl;
    std::cout << "Page size: " << getpagesize() << " bytes" << std::endl;
    std::cout << "Starting performance tests..." << std::endl;

    StreamFixParserPerformanceTest perf_test;
    bool success = perf_test.runAllPerformanceTests();

    return success ? 0 : 1;
}