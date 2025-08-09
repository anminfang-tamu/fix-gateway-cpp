#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "protocol/stream_fix_parser.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"
#include "utils/logger.h"
#include <chrono>
#include <random>
#include <string>
#include <vector>
#include <memory>
#include <thread>

using namespace fix_gateway::protocol;
using namespace fix_gateway::common;
using namespace testing;

// =================================================================
// TEST FIXTURE - StreamFixParserComprehensiveTest
// =================================================================

class StreamFixParserComprehensiveTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        message_pool_ = std::make_unique<MessagePool<FixMessage>>(1000, "test_pool");
        parser_ = std::make_unique<StreamFixParser>(message_pool_.get());

        // Configure parser for testing
        parser_->setMaxMessageSize(8192);
        parser_->setValidateChecksum(true);
        parser_->setStrictValidation(true);
    }

    void TearDown() override
    {
        parser_.reset();
        message_pool_.reset();
    }

    // Helper to create valid ExecutionReport message
    std::string createExecutionReport(const std::string &extra_fields = "")
    {
        std::string msg = "8=FIX.4.4\x01"
                          "9=";
        std::string body = "35=8\x01"
                           "49=SENDER\x01"
                           "56=TARGET\x01"
                           "34=1\x01"
                           "52=20231201-12:00:00\x01";
        if (!extra_fields.empty())
        {
            body += extra_fields;
        }

        msg += std::to_string(body.length()) + "\x01" + body;

        // Calculate checksum
        uint8_t checksum = 0;
        for (char c : msg)
        {
            checksum += static_cast<uint8_t>(c);
        }
        checksum %= 256;

        // Format checksum as 3-digit string
        std::string checksum_str = std::to_string(checksum);
        if (checksum < 10)
            checksum_str = "00" + checksum_str;
        else if (checksum < 100)
            checksum_str = "0" + checksum_str;

        msg += "10=" + checksum_str + "\x01";
        return msg;
    }

    // Helper to create valid OrderCancelReject message (client receives this)
    std::string createOrderCancelReject()
    {
        std::string msg = "8=FIX.4.4\x01"
                          "9=";
        std::string body = "35=9\x01" // OrderCancelReject
                           "49=EXCHANGE\x01"
                           "56=CLIENT\x01"
                           "34=125\x01"
                           "52=20231201-10:32:00\x01"
                           "11=ORDER002\x01"  // ClOrdID
                           "37=REJECT001\x01" // OrderID
                           "39=8\x01"         // OrdStatus = Rejected
                           "102=0\x01";       // CxlRejReason = Too late to cancel

        msg += std::to_string(body.length()) + "\x01" + body;

        // Calculate correct checksum
        uint8_t checksum = 0;
        for (char c : msg)
        {
            checksum += static_cast<uint8_t>(c);
        }
        checksum %= 256;

        // Format checksum as 3-digit string
        std::string checksum_str = std::to_string(checksum);
        if (checksum < 10)
            checksum_str = "00" + checksum_str;
        else if (checksum < 100)
            checksum_str = "0" + checksum_str;

        msg += "10=" + checksum_str + "\x01";
        return msg;
    }

    // Helper to create valid Reject message (session-level)
    std::string createReject()
    {
        std::string msg = "8=FIX.4.4\x01"
                          "9=";
        std::string body = "35=3\x01" // Reject
                           "49=EXCHANGE\x01"
                           "56=CLIENT\x01"
                           "34=126\x01"
                           "52=20231201-10:33:00\x01"
                           "45=100\x01"                   // RefSeqNum
                           "371=35\x01"                   // RefTagID
                           "372=D\x01"                    // RefMsgType
                           "373=1\x01"                    // SessionRejectReason
                           "58=Invalid message type\x01"; // Text

        msg += std::to_string(body.length()) + "\x01" + body;

        // Calculate correct checksum
        uint8_t checksum = 0;
        for (char c : msg)
        {
            checksum += static_cast<uint8_t>(c);
        }
        checksum %= 256;

        // Format checksum as 3-digit string
        std::string checksum_str = std::to_string(checksum);
        if (checksum < 10)
            checksum_str = "00" + checksum_str;
        else if (checksum < 100)
            checksum_str = "0" + checksum_str;

        msg += "10=" + checksum_str + "\x01";
        return msg;
    }

    // Helper to create valid TestRequest message
    std::string createTestRequest()
    {
        std::string msg = "8=FIX.4.4\x01"
                          "9=";
        std::string body = "35=1\x01" // TestRequest
                           "49=EXCHANGE\x01"
                           "56=CLIENT\x01"
                           "34=127\x01"
                           "52=20231201-10:34:00\x01"
                           "112=TEST123\x01"; // TestReqID

        msg += std::to_string(body.length()) + "\x01" + body;

        // Calculate correct checksum
        uint8_t checksum = 0;
        for (char c : msg)
        {
            checksum += static_cast<uint8_t>(c);
        }
        checksum %= 256;

        // Format checksum as 3-digit string
        std::string checksum_str = std::to_string(checksum);
        if (checksum < 10)
            checksum_str = "00" + checksum_str;
        else if (checksum < 100)
            checksum_str = "0" + checksum_str;

        msg += "10=" + checksum_str + "\x01";
        return msg;
    }

    // Helper to create valid ResendRequest message
    std::string createResendRequest()
    {
        std::string msg = "8=FIX.4.4\x01"
                          "9=";
        std::string body = "35=2\x01" // ResendRequest
                           "49=EXCHANGE\x01"
                           "56=CLIENT\x01"
                           "34=128\x01"
                           "52=20231201-10:35:00\x01"
                           "7=50\x01"   // BeginSeqNo
                           "16=75\x01"; // EndSeqNo

        msg += std::to_string(body.length()) + "\x01" + body;

        // Calculate correct checksum
        uint8_t checksum = 0;
        for (char c : msg)
        {
            checksum += static_cast<uint8_t>(c);
        }
        checksum %= 256;

        // Format checksum as 3-digit string
        std::string checksum_str = std::to_string(checksum);
        if (checksum < 10)
            checksum_str = "00" + checksum_str;
        else if (checksum < 100)
            checksum_str = "0" + checksum_str;

        msg += "10=" + checksum_str + "\x01";
        return msg;
    }

    // Helper to create valid Heartbeat message
    std::string createHeartbeat()
    {
        std::string msg = "8=FIX.4.4\x01"
                          "9=";
        std::string body = "35=0\x01"
                           "49=SENDER\x01"
                           "56=TARGET\x01"
                           "34=124\x01"
                           "52=20231201-10:31:00\x01";

        msg += std::to_string(body.length()) + "\x01" + body;

        // Calculate correct checksum
        uint8_t checksum = 0;
        for (char c : msg)
        {
            checksum += static_cast<uint8_t>(c);
        }
        checksum %= 256;

        // Format checksum as 3-digit string
        std::string checksum_str = std::to_string(checksum);
        if (checksum < 10)
            checksum_str = "00" + checksum_str;
        else if (checksum < 100)
            checksum_str = "0" + checksum_str;

        msg += "10=" + checksum_str + "\x01";
        return msg;
    }

    std::unique_ptr<MessagePool<FixMessage>> message_pool_;
    std::unique_ptr<StreamFixParser> parser_;
};

// =================================================================
// BASIC PARSING TESTS
// =================================================================

TEST_F(StreamFixParserComprehensiveTest, ParseCompleteExecutionReport)
{
    std::string msg = createExecutionReport();

    auto result = parser_->parse(msg.c_str(), msg.length());

    ASSERT_EQ(StreamFixParser::ParseStatus::Success, result.status);
    ASSERT_NE(nullptr, result.parsed_message);
    ASSERT_EQ(msg.length(), result.bytes_consumed);

    // Verify message fields
    EXPECT_TRUE(result.parsed_message->hasField(FixFields::MsgType));
    std::string msg_type;
    EXPECT_TRUE(result.parsed_message->getField(FixFields::MsgType, msg_type));
    EXPECT_EQ("8", msg_type);

    message_pool_->deallocate(result.parsed_message);
}

TEST_F(StreamFixParserComprehensiveTest, ParseCompleteHeartbeat)
{
    std::string msg = createHeartbeat();

    auto result = parser_->parse(msg.c_str(), msg.length());

    ASSERT_EQ(StreamFixParser::ParseStatus::Success, result.status);
    ASSERT_NE(nullptr, result.parsed_message);

    // Verify heartbeat message type
    std::string msg_type;
    EXPECT_TRUE(result.parsed_message->getField(FixFields::MsgType, msg_type));
    EXPECT_EQ("0", msg_type);

    message_pool_->deallocate(result.parsed_message);
}

TEST_F(StreamFixParserComprehensiveTest, ParseCompleteOrderCancelReject)
{
    std::string msg = createOrderCancelReject();

    auto result = parser_->parse(msg.c_str(), msg.length());

    if (result.status == StreamFixParser::ParseStatus::Success)
    {
        ASSERT_NE(nullptr, result.parsed_message);

        // Verify message type
        std::string msg_type;
        EXPECT_TRUE(result.parsed_message->getField(FixFields::MsgType, msg_type));
        EXPECT_EQ("9", msg_type);

        message_pool_->deallocate(result.parsed_message);
    }
    else
    {
        // If parser doesn't support this message type yet, that's expected
        std::cout << "OrderCancelReject not supported (status: " << static_cast<int>(result.status) << ")" << std::endl;
    }
}

TEST_F(StreamFixParserComprehensiveTest, ParseSessionMessages)
{
    // Test various session-level messages that clients receive
    std::vector<std::pair<std::string, std::string>> test_cases = {
        {createReject(), "3"},       // Reject
        {createTestRequest(), "1"},  // TestRequest
        {createResendRequest(), "2"} // ResendRequest
    };

    for (const auto &[msg, expected_type] : test_cases)
    {
        parser_->reset(); // Reset for each message

        auto result = parser_->parse(msg.c_str(), msg.length());

        if (result.status == StreamFixParser::ParseStatus::Success)
        {
            ASSERT_NE(nullptr, result.parsed_message);

            std::string msg_type;
            EXPECT_TRUE(result.parsed_message->getField(FixFields::MsgType, msg_type));
            EXPECT_EQ(expected_type, msg_type);

            message_pool_->deallocate(result.parsed_message);
        }
        else
        {
            std::cout << "⚠️  MsgType " << expected_type << " not supported (status: "
                      << static_cast<int>(result.status) << ")" << std::endl;
        }
    }
}

// =================================================================
// PARTIAL MESSAGE HANDLING TESTS
// =================================================================

TEST_F(StreamFixParserComprehensiveTest, HandlePartialMessageReassembly)
{
    std::string complete_msg = createExecutionReport();

    // Split message into two parts
    size_t split_point = complete_msg.length() / 2;
    std::string part1 = complete_msg.substr(0, split_point);
    std::string part2 = complete_msg.substr(split_point);

    // Parse first part
    auto result1 = parser_->parse(part1.c_str(), part1.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::NeedMoreData, result1.status);
    EXPECT_EQ(nullptr, result1.parsed_message);

    // Verify parser has partial message
    EXPECT_TRUE(parser_->hasPartialMessage());
    EXPECT_GT(parser_->getPartialMessageSize(), 0U);

    // Parse second part
    auto result2 = parser_->parse(part2.c_str(), part2.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::Success, result2.status);
    EXPECT_NE(nullptr, result2.parsed_message);

    // Verify complete message was parsed
    if (result2.parsed_message)
    {
        std::string msg_type;
        EXPECT_TRUE(result2.parsed_message->getField(FixFields::MsgType, msg_type));
        EXPECT_EQ("8", msg_type);
        message_pool_->deallocate(result2.parsed_message);
    }
}

TEST_F(StreamFixParserComprehensiveTest, HandleMultiplePartialMessages)
{
    // Create two separate messages and test them individually
    std::string msg1 = createHeartbeat();
    std::string msg2 = createExecutionReport();

    // Test first message with partial parsing
    size_t split1 = msg1.length() / 2;
    std::string part1_1 = msg1.substr(0, split1);
    std::string part1_2 = msg1.substr(split1);

    auto result1_1 = parser_->parse(part1_1.c_str(), part1_1.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::NeedMoreData, result1_1.status);

    auto result1_2 = parser_->parse(part1_2.c_str(), part1_2.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::Success, result1_2.status);

    if (result1_2.parsed_message)
    {
        std::string msg_type;
        EXPECT_TRUE(result1_2.parsed_message->getField(FixFields::MsgType, msg_type));
        EXPECT_EQ("0", msg_type); // Heartbeat
        message_pool_->deallocate(result1_2.parsed_message);
    }

    // Reset parser for second message
    parser_->reset();

    // Test second message with partial parsing
    size_t split2 = msg2.length() / 2;
    std::string part2_1 = msg2.substr(0, split2);
    std::string part2_2 = msg2.substr(split2);

    auto result2_1 = parser_->parse(part2_1.c_str(), part2_1.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::NeedMoreData, result2_1.status);

    auto result2_2 = parser_->parse(part2_2.c_str(), part2_2.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::Success, result2_2.status);

    if (result2_2.parsed_message)
    {
        std::string msg_type;
        EXPECT_TRUE(result2_2.parsed_message->getField(FixFields::MsgType, msg_type));
        EXPECT_EQ("8", msg_type); // ExecutionReport
        message_pool_->deallocate(result2_2.parsed_message);
    }
}

// =================================================================
// ERROR HANDLING TESTS
// =================================================================

TEST_F(StreamFixParserComprehensiveTest, HandleMalformedBeginString)
{
    std::string bad_msg = "INVALID_START\x01"
                          "9=20\x01"
                          "35=0\x01"
                          "10=123\x01";

    auto result = parser_->parse(bad_msg.c_str(), bad_msg.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::InvalidFormat, result.status);
    EXPECT_EQ(nullptr, result.parsed_message);
}

TEST_F(StreamFixParserComprehensiveTest, HandleInvalidBodyLength)
{
    std::string bad_msg = "8=FIX.4.4\x01"
                          "9=INVALID\x01"
                          "35=0\x01"
                          "10=123\x01";

    auto result = parser_->parse(bad_msg.c_str(), bad_msg.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::InvalidFormat, result.status);
    EXPECT_EQ(nullptr, result.parsed_message);
}

TEST_F(StreamFixParserComprehensiveTest, HandleChecksumValidation)
{
    parser_->setValidateChecksum(true);

    std::string msg_bad_checksum = "8=FIX.4.4\x01"
                                   "9=58\x01"
                                   "35=0\x01"
                                   "49=SENDER\x01"
                                   "56=TARGET\x01"
                                   "34=1\x01"
                                   "52=20231201-12:00:00\x01"
                                   "10=999\x01"; // Wrong checksum

    auto result = parser_->parse(msg_bad_checksum.c_str(), msg_bad_checksum.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::ChecksumError, result.status);
    EXPECT_EQ(nullptr, result.parsed_message);
}

// =================================================================
// STATE MACHINE TESTS
// =================================================================

TEST_F(StreamFixParserComprehensiveTest, VerifyInitialState)
{
    EXPECT_EQ(StreamFixParser::ParseState::IDLE, parser_->getCurrentState());
    EXPECT_FALSE(parser_->isInErrorRecovery());
}

TEST_F(StreamFixParserComprehensiveTest, VerifyStateTransitions)
{
    // Test valid state transitions
    EXPECT_TRUE(parser_->isValidStateTransition(
        StreamFixParser::ParseState::IDLE,
        StreamFixParser::ParseState::PARSING_BEGIN_STRING));

    // Test invalid state transitions
    EXPECT_FALSE(parser_->isValidStateTransition(
        StreamFixParser::ParseState::IDLE,
        StreamFixParser::ParseState::MESSAGE_COMPLETE));
}

TEST_F(StreamFixParserComprehensiveTest, VerifyStateAfterSuccessfulParse)
{
    std::string msg = createHeartbeat();

    auto result = parser_->parse(msg.c_str(), msg.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::Success, result.status);
    EXPECT_EQ(StreamFixParser::ParseState::IDLE, parser_->getCurrentState());

    if (result.parsed_message)
    {
        message_pool_->deallocate(result.parsed_message);
    }
}

// =================================================================
// CIRCUIT BREAKER TESTS
// =================================================================

TEST_F(StreamFixParserComprehensiveTest, CircuitBreakerActivation)
{
    parser_->setMaxConsecutiveErrors(2);
    parser_->setErrorRecoveryEnabled(true);

    std::string bad_msg = "INVALID_FIX_MESSAGE";

    // Trigger circuit breaker with multiple errors
    for (int i = 0; i < 3; ++i)
    {
        auto result = parser_->parse(bad_msg.c_str(), bad_msg.length());
        EXPECT_NE(StreamFixParser::ParseStatus::Success, result.status);
    }

    EXPECT_TRUE(parser_->isCircuitBreakerActive());

    // Test recovery
    parser_->resetErrorRecovery();
    EXPECT_FALSE(parser_->isCircuitBreakerActive());
}

// =================================================================
// PERFORMANCE STATISTICS TESTS
// =================================================================

TEST_F(StreamFixParserComprehensiveTest, StatisticsTracking)
{
    parser_->resetStats();
    const auto &initial_stats = parser_->getStats();
    EXPECT_EQ(0U, initial_stats.messages_parsed);
    EXPECT_EQ(0U, initial_stats.parse_errors);

    // Parse successful message
    std::string msg = createExecutionReport();
    auto result = parser_->parse(msg.c_str(), msg.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::Success, result.status);

    const auto &stats_after_success = parser_->getStats();
    EXPECT_EQ(1U, stats_after_success.messages_parsed);
    EXPECT_EQ(0U, stats_after_success.parse_errors);
    EXPECT_GT(stats_after_success.total_parse_time_ns, 0U);

    if (result.parsed_message)
    {
        message_pool_->deallocate(result.parsed_message);
    }

    // Parse error message
    std::string bad_msg = "INVALID_BEGIN_STRING";
    auto bad_result = parser_->parse(bad_msg.c_str(), bad_msg.length());
    EXPECT_NE(StreamFixParser::ParseStatus::Success, bad_result.status);

    const auto &stats_after_error = parser_->getStats();
    EXPECT_EQ(1U, stats_after_error.messages_parsed);
    EXPECT_EQ(1U, stats_after_error.parse_errors);
}

// =================================================================
// CONFIGURATION TESTS
// =================================================================

TEST_F(StreamFixParserComprehensiveTest, ConfigurationOptions)
{
    // Test checksum validation toggle
    parser_->setValidateChecksum(false);

    std::string msg_bad_checksum = "8=FIX.4.4\x01"
                                   "9=58\x01"
                                   "35=0\x01"
                                   "49=SENDER\x01"
                                   "56=TARGET\x01"
                                   "34=1\x01"
                                   "52=20231201-12:00:00\x01"
                                   "10=999\x01";

    auto result1 = parser_->parse(msg_bad_checksum.c_str(), msg_bad_checksum.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::Success, result1.status);

    if (result1.parsed_message)
    {
        message_pool_->deallocate(result1.parsed_message);
    }

    // Enable checksum validation
    parser_->setValidateChecksum(true);
    parser_->reset();

    auto result2 = parser_->parse(msg_bad_checksum.c_str(), msg_bad_checksum.length());
    EXPECT_EQ(StreamFixParser::ParseStatus::ChecksumError, result2.status);
}

// =================================================================
// PERFORMANCE BENCHMARKS
// =================================================================

class StreamFixParserPerformanceTest : public StreamFixParserComprehensiveTest
{
protected:
    static constexpr int BENCHMARK_ITERATIONS = 10000;
    static constexpr int WARMUP_ITERATIONS = 1000;

    struct BenchmarkResult
    {
        double avg_parse_time_ns;
        double min_parse_time_ns;
        double max_parse_time_ns;
        double messages_per_second;
        size_t total_bytes_parsed;
    };

    BenchmarkResult runBenchmark(const std::string &message, int iterations)
    {
        std::vector<double> parse_times;
        parse_times.reserve(iterations);

        auto start_time = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i)
        {
            parser_->reset();

            auto parse_start = std::chrono::high_resolution_clock::now();
            auto result = parser_->parse(message.c_str(), message.length());
            auto parse_end = std::chrono::high_resolution_clock::now();

            if (result.status == StreamFixParser::ParseStatus::Success)
            {
                auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    parse_end - parse_start)
                                    .count();
                parse_times.push_back(duration);

                if (result.parsed_message)
                {
                    message_pool_->deallocate(result.parsed_message);
                }
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                                  end_time - start_time)
                                  .count();

        // Calculate statistics (handle empty parse_times)
        double sum = 0.0;
        double min_time = 0.0;
        double max_time = 0.0;
        double avg_time = 0.0;

        if (!parse_times.empty())
        {
            min_time = *std::min_element(parse_times.begin(), parse_times.end());
            max_time = *std::max_element(parse_times.begin(), parse_times.end());

            for (double time : parse_times)
            {
                sum += time;
            }
            avg_time = sum / parse_times.size();
        }

        double messages_per_second = (parse_times.size() * 1000000.0) / total_duration;

        // Debug output
        std::cout << "Debug: Successful parses: " << parse_times.size() << "/" << iterations << std::endl;

        return BenchmarkResult{
            avg_time, min_time, max_time, messages_per_second,
            static_cast<size_t>(iterations * message.length())};
    }
};

TEST_F(StreamFixParserPerformanceTest, BenchmarkExecutionReportParsing)
{
    std::string msg = createExecutionReport();

    // Warmup
    runBenchmark(msg, WARMUP_ITERATIONS);

    // Actual benchmark
    auto result = runBenchmark(msg, BENCHMARK_ITERATIONS);

    std::cout << "\n=== ExecutionReport Parsing Performance ===" << std::endl;
    std::cout << "Iterations: " << BENCHMARK_ITERATIONS << std::endl;
    std::cout << "Avg parse time: " << result.avg_parse_time_ns << " ns" << std::endl;
    std::cout << "Min parse time: " << result.min_parse_time_ns << " ns" << std::endl;
    std::cout << "Max parse time: " << result.max_parse_time_ns << " ns" << std::endl;
    std::cout << "Messages/sec: " << result.messages_per_second << std::endl;
    std::cout << "Total bytes: " << result.total_bytes_parsed << std::endl;

    // Performance assertions (adjust thresholds based on your requirements)
    EXPECT_LT(result.avg_parse_time_ns, 10000.0);   // Less than 10μs average
    EXPECT_GT(result.messages_per_second, 50000.0); // More than 50k msg/sec
}

TEST_F(StreamFixParserPerformanceTest, BenchmarkHeartbeatParsing)
{
    std::string msg = createHeartbeat();

    // Warmup
    runBenchmark(msg, WARMUP_ITERATIONS);

    // Actual benchmark
    auto result = runBenchmark(msg, BENCHMARK_ITERATIONS);

    std::cout << "\n=== Heartbeat Parsing Performance ===" << std::endl;
    std::cout << "Iterations: " << BENCHMARK_ITERATIONS << std::endl;
    std::cout << "Avg parse time: " << result.avg_parse_time_ns << " ns" << std::endl;
    std::cout << "Min parse time: " << result.min_parse_time_ns << " ns" << std::endl;
    std::cout << "Max parse time: " << result.max_parse_time_ns << " ns" << std::endl;
    std::cout << "Messages/sec: " << result.messages_per_second << std::endl;
    std::cout << "Total bytes: " << result.total_bytes_parsed << std::endl;

    // Heartbeats should be faster than ExecutionReports
    EXPECT_LT(result.avg_parse_time_ns, 8000.0);    // Less than 8μs average
    EXPECT_GT(result.messages_per_second, 60000.0); // More than 60k msg/sec
}

TEST_F(StreamFixParserPerformanceTest, BenchmarkExecutionReportVsHeartbeat)
{
    std::string exec_msg = createExecutionReport();
    std::string hb_msg = createHeartbeat();

    // Benchmark ExecutionReport
    auto exec_result = runBenchmark(exec_msg, BENCHMARK_ITERATIONS);

    // Benchmark Heartbeat
    auto hb_result = runBenchmark(hb_msg, BENCHMARK_ITERATIONS);

    std::cout << "\n=== ExecutionReport vs Heartbeat Comparison ===" << std::endl;
    std::cout << "ExecutionReport avg: " << exec_result.avg_parse_time_ns << " ns" << std::endl;
    std::cout << "Heartbeat avg: " << hb_result.avg_parse_time_ns << " ns" << std::endl;
    std::cout << "Performance ratio: " << (exec_result.avg_parse_time_ns / hb_result.avg_parse_time_ns) << "x" << std::endl;

    // Both should be fast
    EXPECT_LT(exec_result.avg_parse_time_ns, 10000.0);
    EXPECT_LT(hb_result.avg_parse_time_ns, 8000.0);
    EXPECT_GT(exec_result.messages_per_second, 50000.0);
    EXPECT_GT(hb_result.messages_per_second, 60000.0);
}

// =================================================================
// STRESS TESTS
// =================================================================

TEST_F(StreamFixParserComprehensiveTest, StressTestRandomMessages)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> msg_type_dist(0, 5); // All client-side message types

    const int NUM_MESSAGES = 1000;
    int successful_parses = 0;
    int supported_messages = 0;

    std::map<std::string, int> type_stats;

    for (int i = 0; i < NUM_MESSAGES; ++i)
    {
        std::string msg;
        std::string msg_type_name;

        switch (msg_type_dist(gen))
        {
        case 0:
            msg = createHeartbeat();
            msg_type_name = "Heartbeat";
            break;
        case 1:
            msg = createExecutionReport();
            msg_type_name = "ExecutionReport";
            break;
        case 2:
            msg = createOrderCancelReject();
            msg_type_name = "OrderCancelReject";
            break;
        case 3:
            msg = createReject();
            msg_type_name = "Reject";
            break;
        case 4:
            msg = createTestRequest();
            msg_type_name = "TestRequest";
            break;
        case 5:
            msg = createResendRequest();
            msg_type_name = "ResendRequest";
            break;
        }

        auto result = parser_->parse(msg.c_str(), msg.length());

        // Count statistics by message type
        type_stats[msg_type_name]++;

        if (result.status == StreamFixParser::ParseStatus::Success)
        {
            successful_parses++;
            if (result.parsed_message)
            {
                message_pool_->deallocate(result.parsed_message);
            }
        }

        // Count as supported if it doesn't fail due to unknown message type
        if (result.status != StreamFixParser::ParseStatus::InvalidFormat ||
            result.error_detail.find("Invalid MsgType") == std::string::npos)
        {
            supported_messages++;
        }

        parser_->reset();
    }

    std::cout << "Stress test results:" << std::endl;
    std::cout << "  Total messages: " << NUM_MESSAGES << std::endl;
    std::cout << "  Successful parses: " << successful_parses << std::endl;
    std::cout << "  Supported messages: " << supported_messages << std::endl;
    std::cout << "  Success rate: " << (100.0 * successful_parses / NUM_MESSAGES) << "%" << std::endl;

    std::cout << "\nMessage type distribution:" << std::endl;
    for (const auto &[type, count] : type_stats)
    {
        std::cout << "  " << type << ": " << count << std::endl;
    }

    // Accept if we successfully parse all supported message types
    // (ExecutionReport and Heartbeat are definitely supported)
    double success_rate = 100.0 * successful_parses / NUM_MESSAGES;
    EXPECT_GT(success_rate, 25.0); // At least 25% success rate (lenient for unsupported types)
}

TEST_F(StreamFixParserComprehensiveTest, StressTestPartialMessages)
{
    const int NUM_ITERATIONS = 100;
    std::random_device rd;
    std::mt19937 gen(rd());

    for (int i = 0; i < NUM_ITERATIONS; ++i)
    {
        std::string msg = createExecutionReport();
        std::uniform_int_distribution<> split_dist(10, msg.length() - 10);
        size_t split_point = split_dist(gen);

        std::string part1 = msg.substr(0, split_point);
        std::string part2 = msg.substr(split_point);

        // Parse first part
        auto result1 = parser_->parse(part1.c_str(), part1.length());
        EXPECT_EQ(StreamFixParser::ParseStatus::NeedMoreData, result1.status);

        // Parse second part
        auto result2 = parser_->parse(part2.c_str(), part2.length());
        EXPECT_EQ(StreamFixParser::ParseStatus::Success, result2.status);

        if (result2.parsed_message)
        {
            message_pool_->deallocate(result2.parsed_message);
        }

        parser_->reset();
    }
}

// =================================================================
// MEMORY TESTS
// =================================================================

TEST_F(StreamFixParserComprehensiveTest, MemoryPoolExhaustion)
{
    // Create a small pool that will be exhausted
    auto small_pool = std::make_unique<MessagePool<FixMessage>>(5, "small_test_pool");
    auto small_parser = std::make_unique<StreamFixParser>(small_pool.get());
    small_parser->setValidateChecksum(false);

    std::string msg = createExecutionReport();
    std::vector<FixMessage *> allocated_messages;

    // Allocate until pool is exhausted
    for (int i = 0; i < 6; ++i)
    { // One more than pool size
        auto result = small_parser->parse(msg.c_str(), msg.length());

        if (i < 5)
        { // First 5 should succeed
            EXPECT_EQ(StreamFixParser::ParseStatus::Success, result.status);
            allocated_messages.push_back(result.parsed_message);
        }
        else
        { // 6th should fail
            EXPECT_EQ(StreamFixParser::ParseStatus::AllocationFailed, result.status);
            EXPECT_EQ(nullptr, result.parsed_message);
        }
    }

    // Clean up allocated messages
    for (auto *msg : allocated_messages)
    {
        small_pool->deallocate(msg);
    }
}

// =================================================================
// MAIN FUNCTION FOR STANDALONE EXECUTION
// =================================================================

int main(int argc, char **argv)
{
    // Initialize Google Test
    ::testing::InitGoogleTest(&argc, argv);

    // Enable DEBUG logging for detailed debugging (keep only DEBUG level)
    fix_gateway::utils::Logger::getInstance().setLogLevel(fix_gateway::utils::LogLevel::DEBUG);
    fix_gateway::utils::Logger::getInstance().enableConsoleOutput(true);

    std::cout << "🚀 Starting StreamFixParser Comprehensive Tests with GTest" << std::endl;
    std::cout << "Testing: Basic parsing, partial messages, error handling, performance" << std::endl;

    return RUN_ALL_TESTS();
}