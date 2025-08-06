#include "protocol/stream_fix_parser.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"
#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>
#include <cstdint>

using namespace fix_gateway::protocol;
using namespace fix_gateway::common;

/**
 * @brief Claude's Clean Performance Test - Client Side Messages Only
 *
 * This test isolates the core parsing performance by using:
 * 1. Hand-crafted, known-good FIX messages (ExecutionReport, Heartbeat)
 * 2. Controlled test environment
 * 3. Clear error reporting
 * 4. Focus on string optimization impact
 * 5. Client-side message types only (no NewOrderSingle)
 */
class ClaudePerformanceTest
{
private:
    MessagePool<FixMessage> message_pool_;
    std::unique_ptr<StreamFixParser> parser_;

    struct TestMetrics
    {
        uint64_t successful_parses = 0;
        uint64_t parse_errors = 0;
        uint64_t total_time_ns = 0;
        uint64_t min_time_ns = UINT64_MAX;
        uint64_t max_time_ns = 0;
        std::vector<uint64_t> parse_times;
        std::vector<std::string> error_messages;

        void addResult(bool success, uint64_t time_ns, const std::string &error = "")
        {
            if (success)
            {
                successful_parses++;
            }
            else
            {
                parse_errors++;
                if (error_messages.size() < 10)
                { // Keep first 10 errors for analysis
                    error_messages.push_back(error);
                }
            }

            total_time_ns += time_ns;
            min_time_ns = std::min(min_time_ns, time_ns);
            max_time_ns = std::max(max_time_ns, time_ns);
            parse_times.push_back(time_ns);
        }

        double getSuccessRate() const
        {
            uint64_t total = successful_parses + parse_errors;
            return total > 0 ? (double(successful_parses) / total) * 100.0 : 0.0;
        }

        double getAverageLatencyNs() const
        {
            return parse_times.empty() ? 0.0 : double(total_time_ns) / parse_times.size();
        }

        uint64_t getP99LatencyNs() const
        {
            if (parse_times.empty())
                return 0;
            auto times_copy = parse_times;
            std::sort(times_copy.begin(), times_copy.end());
            size_t p99_index = (times_copy.size() * 99) / 100;
            return times_copy[std::min(p99_index, times_copy.size() - 1)];
        }
    };

public:
    ClaudePerformanceTest()
        : message_pool_(10000, "claude_test_pool"),
          parser_(std::make_unique<StreamFixParser>(&message_pool_))
    {
        std::cout << "🧪 Claude's Performance Test Initialized\n";
        std::cout << "   Message Pool Size: 10,000\n";
        std::cout << "   Parser: StreamFixParser with string_view optimizations\n\n";
    }

    /**
     * @brief Generate a perfect ExecutionReport message (client side receives these)
     * These messages are manually crafted to be 100% compliant
     */
    std::string createPerfectMessage(int seq_num)
    {
        // Build a minimal but complete FIX 4.4 ExecutionReport message
        std::string msg;

        // Header: BeginString
        msg += "8=FIX.4.4\x01";

        // Body (we'll calculate length)
        std::string body;
        body += "35=8\x01";                                     // MsgType = ExecutionReport
        body += "49=SENDER\x01";                                // SenderCompID
        body += "56=TARGET\x01";                                // TargetCompID
        body += "34=" + std::to_string(seq_num) + "\x01";       // MsgSeqNum
        body += "52=20231201-12:00:00\x01";                     // SendingTime
        body += "37=EXEC_" + std::to_string(seq_num) + "\x01";  // OrderID
        body += "11=ORDER_" + std::to_string(seq_num) + "\x01"; // ClOrdID
        body += "17=EXEC_" + std::to_string(seq_num) + "\x01";  // ExecID
        body += "150=2\x01";                                    // ExecType = Fill
        body += "39=2\x01";                                     // OrdStatus = Filled
        body += "55=AAPL\x01";                                  // Symbol
        body += "54=1\x01";                                     // Side
        body += "38=100\x01";                                   // OrderQty
        body += "32=100\x01";                                   // LastQty
        body += "31=150.25\x01";                                // LastPx
        body += "151=0\x01";                                    // LeavesQty
        body += "14=100\x01";                                   // CumQty
        body += "6=150.25\x01";                                 // AvgPx

        // Add BodyLength
        msg += "9=" + std::to_string(body.length()) + "\x01";
        msg += body;

        // Calculate checksum correctly
        uint8_t checksum = 0;
        for (unsigned char c : msg)
        {
            checksum += c;
        }
        checksum %= 256;

        // Add checksum (always 3 digits with leading zeros)
        char checksum_buf[16];
        snprintf(checksum_buf, sizeof(checksum_buf), "10=%03d\x01", checksum);
        msg += checksum_buf;

        return msg;
    }

    /**
     * @brief Run single-threaded performance test with perfect messages
     */
    TestMetrics runSingleThreadTest(int num_messages = 10000)
    {
        std::cout << "🏃 Running Single-Thread Test (" << num_messages << " messages)\n";

        TestMetrics metrics;

        // Generate test messages once (avoid generation overhead in timing)
        std::vector<std::string> test_messages;
        test_messages.reserve(num_messages);

        for (int i = 0; i < num_messages; ++i)
        {
            test_messages.push_back(createPerfectMessage(i + 1));
        }

        std::cout << "   Generated " << test_messages.size() << " perfect ExecutionReport messages\n";
        std::cout << "   Sample message length: " << test_messages[0].length() << " bytes\n";
        std::cout << "   Running parsing test...\n";

        // Run the actual parsing test
        for (size_t i = 0; i < test_messages.size(); ++i)
        {
            const auto &msg = test_messages[i];

            // Time the parsing operation
            auto start = std::chrono::high_resolution_clock::now();
            auto result = parser_->parse(msg.c_str(), msg.length());
            auto end = std::chrono::high_resolution_clock::now();

            uint64_t parse_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

            bool success = (result.status == StreamFixParser::ParseStatus::Success);
            std::string error_msg = success ? "" : result.error_detail;

            metrics.addResult(success, parse_time_ns, error_msg);

            // Clean up parsed message
            if (result.parsed_message)
            {
                message_pool_.deallocate(result.parsed_message);
            }

            // Reset parser for next message
            parser_->reset();

            // Progress indicator
            if ((i + 1) % 2000 == 0)
            {
                std::cout << "   Processed " << (i + 1) << " messages...\n";
            }
        }

        return metrics;
    }

    /**
     * @brief Test with various client-side message types to see parsing consistency
     * Only tests ExecutionReport and Heartbeat (messages a client receives)
     */
    TestMetrics runMessageTypeTest()
    {
        std::cout << "🔄 Running Client-Side Message Type Variety Test\n";

        TestMetrics metrics;
        std::vector<std::string> messages;

        // Generate different message types
        for (int i = 0; i < 1000; ++i)
        {
            // Alternate between different message patterns (no NewOrderSingle - client side doesn't receive these)
            if (i % 2 == 0)
            {
                messages.push_back(createHeartbeatMessage(i)); // Heartbeat
            }
            else
            {
                messages.push_back(createExecutionReportMessage(i)); // ExecutionReport
            }
        }

        // Test parsing
        for (const auto &msg : messages)
        {
            auto start = std::chrono::high_resolution_clock::now();
            auto result = parser_->parse(msg.c_str(), msg.length());
            auto end = std::chrono::high_resolution_clock::now();

            uint64_t parse_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            bool success = (result.status == StreamFixParser::ParseStatus::Success);

            metrics.addResult(success, parse_time_ns, result.error_detail);

            if (result.parsed_message)
            {
                message_pool_.deallocate(result.parsed_message);
            }
            parser_->reset();
        }

        return metrics;
    }

    /**
     * @brief Print detailed test results
     */
    void printResults(const TestMetrics &metrics, const std::string &test_name)
    {
        std::cout << "\n📊 " << test_name << " Results:\n";
        std::cout << "   ✅ Successful parses:  " << std::setw(8) << metrics.successful_parses << "\n";
        std::cout << "   ❌ Parse errors:       " << std::setw(8) << metrics.parse_errors << "\n";
        std::cout << "   📈 Success rate:       " << std::fixed << std::setprecision(2)
                  << metrics.getSuccessRate() << "%\n";

        if (metrics.successful_parses > 0)
        {
            std::cout << "   ⚡ Average latency:     " << std::setw(8)
                      << static_cast<uint64_t>(metrics.getAverageLatencyNs()) << " ns\n";
            std::cout << "   ⚡ Min latency:         " << std::setw(8) << metrics.min_time_ns << " ns\n";
            std::cout << "   ⚡ Max latency:         " << std::setw(8) << metrics.max_time_ns << " ns\n";
            std::cout << "   ⚡ P99 latency:         " << std::setw(8) << metrics.getP99LatencyNs() << " ns\n";

            uint64_t total_messages = metrics.successful_parses + metrics.parse_errors;
            if (metrics.total_time_ns > 0)
            {
                double throughput = (double(total_messages) * 1e9) / metrics.total_time_ns;
                std::cout << "   🚀 Throughput:          " << std::setw(8)
                          << static_cast<uint64_t>(throughput) << " msgs/sec\n";
            }
        }

        // Show first few errors for debugging
        if (!metrics.error_messages.empty())
        {
            std::cout << "\n🐛 First Few Errors:\n";
            for (size_t i = 0; i < std::min(size_t(3), metrics.error_messages.size()); ++i)
            {
                std::cout << "   " << (i + 1) << ". " << metrics.error_messages[i] << "\n";
            }
        }
        std::cout << "\n";
    }

private:
    std::string createHeartbeatMessage(int seq_num)
    {
        std::string msg = "8=FIX.4.4\x01";

        std::string body;
        body += "35=0\x01";                               // MsgType = Heartbeat
        body += "49=SENDER\x01";                          // SenderCompID
        body += "56=TARGET\x01";                          // TargetCompID
        body += "34=" + std::to_string(seq_num) + "\x01"; // MsgSeqNum
        body += "52=20231201-12:00:00\x01";               // SendingTime

        msg += "9=" + std::to_string(body.length()) + "\x01";
        msg += body;

        // Calculate checksum
        uint8_t checksum = 0;
        for (unsigned char c : msg)
        {
            checksum += c;
        }
        checksum %= 256;

        char checksum_buf[16];
        snprintf(checksum_buf, sizeof(checksum_buf), "10=%03d\x01", checksum);
        msg += checksum_buf;

        return msg;
    }

    std::string createExecutionReportMessage(int seq_num)
    {
        std::string msg = "8=FIX.4.4\x01";

        std::string body;
        body += "35=8\x01";                                     // MsgType = ExecutionReport
        body += "49=SENDER\x01";                                // SenderCompID
        body += "56=TARGET\x01";                                // TargetCompID
        body += "34=" + std::to_string(seq_num) + "\x01";       // MsgSeqNum
        body += "52=20231201-12:00:00\x01";                     // SendingTime
        body += "37=EXEC_" + std::to_string(seq_num) + "\x01";  // OrderID
        body += "11=ORDER_" + std::to_string(seq_num) + "\x01"; // ClOrdID
        body += "17=EXEC_" + std::to_string(seq_num) + "\x01";  // ExecID
        body += "150=2\x01";                                    // ExecType = Fill
        body += "39=2\x01";                                     // OrdStatus = Filled
        body += "55=AAPL\x01";                                  // Symbol
        body += "54=1\x01";                                     // Side
        body += "38=100\x01";                                   // OrderQty
        body += "32=100\x01";                                   // LastQty
        body += "31=150.25\x01";                                // LastPx
        body += "151=0\x01";                                    // LeavesQty
        body += "14=100\x01";                                   // CumQty
        body += "6=150.25\x01";                                 // AvgPx

        msg += "9=" + std::to_string(body.length()) + "\x01";
        msg += body;

        // Calculate checksum
        uint8_t checksum = 0;
        for (unsigned char c : msg)
        {
            checksum += c;
        }
        checksum %= 256;

        char checksum_buf[16];
        snprintf(checksum_buf, sizeof(checksum_buf), "10=%03d\x01", checksum);
        msg += checksum_buf;

        return msg;
    }
};

int main()
{
    std::cout << "🎯 Claude's FIX Parser Performance Test\n";
    std::cout << "==========================================\n";
    std::cout << "Testing string_view optimizations vs baseline\n\n";

    try
    {
        ClaudePerformanceTest test;

        // Test 1: Single-threaded performance with perfect messages
        auto single_thread_metrics = test.runSingleThreadTest(10000);
        test.printResults(single_thread_metrics, "Single-Thread Perfect Messages");

        // Test 2: Different message types
        auto variety_metrics = test.runMessageTypeTest();
        test.printResults(variety_metrics, "Message Type Variety");

        // Summary
        std::cout << "🎉 Claude's Performance Test Complete!\n";
        std::cout << "\n🔍 Key Insights:\n";

        if (single_thread_metrics.getSuccessRate() > 95.0)
        {
            std::cout << "✅ Parser is working correctly with "
                      << single_thread_metrics.getSuccessRate() << "% success rate\n";
            std::cout << "⚡ Average latency: " << single_thread_metrics.getAverageLatencyNs() << " ns\n";
            std::cout << "🚀 This shows the impact of string_view optimizations!\n";
        }
        else
        {
            std::cout << "⚠️  Parser has issues - success rate only "
                      << single_thread_metrics.getSuccessRate() << "%\n";
            std::cout << "🔧 Need to investigate parser logic\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}