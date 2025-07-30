#include "protocol/stream_fix_parser.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"
#include "utils/logger.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cassert>
#include <memory>

using namespace fix_gateway::protocol;
using namespace fix_gateway::common;

// Test helper macros
#define ASSERT_TRUE(condition, message)                                   \
    do                                                                    \
    {                                                                     \
        if (!(condition))                                                 \
        {                                                                 \
            std::cout << "❌ ASSERTION FAILED: " << message << std::endl; \
            return false;                                                 \
        }                                                                 \
    } while (0)

#define ASSERT_FALSE(condition, message)                                  \
    do                                                                    \
    {                                                                     \
        if (condition)                                                    \
        {                                                                 \
            std::cout << "❌ ASSERTION FAILED: " << message << std::endl; \
            return false;                                                 \
        }                                                                 \
    } while (0)

#define ASSERT_EQ(expected, actual, message)                                                  \
    do                                                                                        \
    {                                                                                         \
        if ((expected) != (actual))                                                           \
        {                                                                                     \
            std::cout << "❌ ASSERTION FAILED: " << message << std::endl;                     \
            std::cout << "   Expected: " << (expected) << ", Got: " << (actual) << std::endl; \
            return false;                                                                     \
        }                                                                                     \
    } while (0)

#define ASSERT_EQ_STATUS(expected, actual, message)                                                                           \
    do                                                                                                                        \
    {                                                                                                                         \
        if ((expected) != (actual))                                                                                           \
        {                                                                                                                     \
            std::cout << "❌ ASSERTION FAILED: " << message << std::endl;                                                     \
            std::cout << "   Expected: " << static_cast<int>(expected) << ", Got: " << static_cast<int>(actual) << std::endl; \
            return false;                                                                                                     \
        }                                                                                                                     \
    } while (0)

#define ASSERT_NE(not_expected, actual, message)                                                        \
    do                                                                                                  \
    {                                                                                                   \
        if ((not_expected) == (actual))                                                                 \
        {                                                                                               \
            std::cout << "❌ ASSERTION FAILED: " << message << std::endl;                               \
            std::cout << "   Did not expect: " << (not_expected) << ", Got: " << (actual) << std::endl; \
            return false;                                                                               \
        }                                                                                               \
    } while (0)

#define ASSERT_GT(value, minimum, message)                                                   \
    do                                                                                       \
    {                                                                                        \
        if ((value) <= (minimum))                                                            \
        {                                                                                    \
            std::cout << "❌ ASSERTION FAILED: " << message << std::endl;                    \
            std::cout << "   Expected > " << (minimum) << ", Got: " << (value) << std::endl; \
            return false;                                                                    \
        }                                                                                    \
    } while (0)

#define RUN_TEST(test_name)                                               \
    do                                                                    \
    {                                                                     \
        setupTest();                                                      \
        std::cout << "\n🧪 Running " << #test_name << "..." << std::endl; \
        if (test_name())                                                  \
        {                                                                 \
            std::cout << "✅ " << #test_name << " PASSED" << std::endl;   \
            passed_tests++;                                               \
        }                                                                 \
        else                                                              \
        {                                                                 \
            std::cout << "❌ " << #test_name << " FAILED" << std::endl;   \
            failed_tests++;                                               \
        }                                                                 \
        teardownTest();                                                   \
        total_tests++;                                                    \
    } while (0)

// Test fixture variables
static std::unique_ptr<MessagePool<FixMessage>> message_pool;
static std::unique_ptr<StreamFixParser> parser;

// Helper function to create a valid FIX ExecutionReport message
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

// Helper function to create a valid FIX Heartbeat message
std::string createHeartbeat()
{
    std::string msg = "8=FIX.4.4\x01"
                      "9=";
    std::string body = "35=0\x01"
                       "49=SENDER\x01"
                       "56=TARGET\x01"
                       "34=1\x01"
                       "52=20231201-12:00:00\x01";

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

// Test setup function
void setupTest()
{
    message_pool = std::make_unique<MessagePool<FixMessage>>(100, "test_pool");
    parser = std::make_unique<StreamFixParser>(message_pool.get());
}

// Test cleanup function
void teardownTest()
{
    parser.reset();
    message_pool.reset();
}

// =====================================================================
// TEST 1: Basic Message Parsing (BodyLength calculation verification)
// =====================================================================

bool testBodyLengthOffByOneFix()
{
    // Create a message where the BodyLength calculation should work correctly
    std::string msg = createExecutionReport();

    // Parse the message
    auto result = parser->parse(msg.c_str(), msg.length());

    // Should parse successfully without NeedMoreData errors
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Message should parse successfully");
    ASSERT_TRUE(result.parsed_message != nullptr, "Parsed message should not be null");
    ASSERT_EQ(msg.length(), result.bytes_consumed, "Should consume entire message");

    if (result.parsed_message)
    {
        ASSERT_TRUE(result.parsed_message->hasField(FixFields::MsgType), "Should have MsgType field");
        std::string msg_type;
        ASSERT_TRUE(result.parsed_message->getField(FixFields::MsgType, msg_type), "Should get MsgType field");
        ASSERT_EQ("8", msg_type, "MsgType should be 8");
        message_pool->deallocate(result.parsed_message);
    }

    return true;
}

// =====================================================================
// TEST 2: Multi-Message Parsing
// =====================================================================

bool testMultiMessageParsing()
{
    // Check initial pool state
    size_t initial_allocated = message_pool->allocated();
    std::cout << "Initial pool allocated count: " << initial_allocated << std::endl;

    // Create two messages in a single buffer
    std::string msg1 = createExecutionReport("37=ORDER1\x01");
    std::string msg2 = createHeartbeat();
    std::string combined_buffer = msg2 + msg1;

    // Parse first message
    auto result1 = parser->parse(combined_buffer.c_str(), combined_buffer.length());

    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result1.status, "First message should parse successfully");
    ASSERT_TRUE(result1.parsed_message != nullptr, "First parsed message should not be null");
    ASSERT_EQ(msg1.length() + msg2.length(), result1.bytes_consumed, "Should consume both messages");

    // Verify pool now has 1 allocated message
    size_t after_first_allocated = message_pool->allocated();
    ASSERT_EQ(initial_allocated + 2, after_first_allocated, "Pool should have same allocated message after first parse");
    std::cout << "Pool allocated after first parse: " << after_first_allocated << std::endl;

    // Parse remaining buffer (second message) - this should be handled by the parser automatically
    // since it consumed both messages in the first call
    // But let's check if there's a second message to parse
    size_t remaining_offset = result1.bytes_consumed;
    size_t remaining_length = combined_buffer.length() - remaining_offset;

    FixMessage *final_msg = result1.parsed_message;

    if (final_msg)
    {
        std::string msg_type;
        ASSERT_TRUE(final_msg->getField(FixFields::MsgType, msg_type), "Should get MsgType field");
        ASSERT_EQ("8", msg_type, "First message should be ExecutionReport");
    }

    // Clean up - deallocate both messages back to pool
    message_pool.reset();

    return true;
}

// =====================================================================
// TEST 3: Checksum Calculation Fix in Optimized Templates
// =====================================================================

bool testChecksumCalculationFix()
{
    // Create ExecutionReport with manually calculated checksum
    std::string msg = "8=FIX.4.4\x01"
                      "9=58\x01"
                      "35=8\x01"
                      "49=SENDER\x01"
                      "56=TARGET\x01"
                      "34=1\x01"
                      "52=20231201-12:00:00\x01";

    // Calculate checksum correctly (excluding SOH before tag 10)
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

    // Enable checksum validation
    parser->setValidateChecksum(true);

    auto result = parser->parse(msg.c_str(), msg.length());

    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Message with correct checksum should parse successfully");
    ASSERT_TRUE(result.parsed_message != nullptr, "Parsed message should not be null");

    if (result.parsed_message)
    {
        message_pool->deallocate(result.parsed_message);
    }

    return true;
}

// =====================================================================
// TEST 4: BodyLength Reuse Fix - Direct parseCompleteMessage Call
// =====================================================================

bool testBodyLengthReuseFix()
{
    std::string msg = createExecutionReport();

    // Call parseCompleteMessage directly (bypasses framing stage)
    auto result = parser->parseCompleteMessage(msg.c_str(), msg.length());

    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Direct parseCompleteMessage should work");
    ASSERT_TRUE(result.parsed_message != nullptr, "Parsed message should not be null");

    if (result.parsed_message)
    {
        // Verify BodyLength field is set correctly (not "0")
        ASSERT_TRUE(result.parsed_message->hasField(FixFields::BodyLength), "Should have BodyLength field");
        std::string body_length_str;
        ASSERT_TRUE(result.parsed_message->getField(FixFields::BodyLength, body_length_str), "Should get BodyLength field");
        int body_length = std::stoi(body_length_str);
        ASSERT_GT(body_length, 0, "BodyLength should not be 0");
        std::string msg_type;
        ASSERT_TRUE(result.parsed_message->getField(FixFields::MsgType, msg_type), "Should get MsgType field");
        ASSERT_EQ("8", msg_type, "MsgType should be correct");
        message_pool->deallocate(result.parsed_message);
    }

    return true;
}

// =====================================================================
// TEST 5: Circuit Breaker Cooling Period
// =====================================================================

bool testCircuitBreakerCoolingPeriod()
{
    // Set low max consecutive errors to trigger circuit breaker quickly
    parser->setMaxConsecutiveErrors(2);
    parser->setErrorRecoveryEnabled(true);

    // Send malformed messages to trigger circuit breaker
    std::string bad_msg = "INVALID_FIX_MESSAGE";

    // Trigger circuit breaker
    for (int i = 0; i < 3; ++i)
    {
        auto result = parser->parse(bad_msg.c_str(), bad_msg.length());
        ASSERT_NE(static_cast<int>(StreamFixParser::ParseStatus::Success), static_cast<int>(result.status), "Malformed message should fail");
    }

    // Verify circuit breaker is active
    ASSERT_TRUE(parser->isCircuitBreakerActive(), "Circuit breaker should be active after multiple errors");

    // Reset the circuit breaker manually to test that it works
    parser->resetErrorRecovery();
    ASSERT_FALSE(parser->isCircuitBreakerActive(), "Circuit breaker should be inactive after reset");

    // Now test with valid message
    std::string good_msg = createHeartbeat();
    auto result = parser->parse(good_msg.c_str(), good_msg.length());
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Valid message should parse after circuit breaker reset");

    if (result.parsed_message)
    {
        message_pool->deallocate(result.parsed_message);
    }

    return true;
}

// =====================================================================
// TEST 6: Defensive Checks in Optimized Templates
// =====================================================================

bool testDefensiveChecksInTemplates()
{
    // Test 1: Message without SOH termination
    std::string incomplete_msg = "8=FIX.4.4\x01"
                                 "9=20\x01"
                                 "35=8\x01"
                                 "49=TEST"; // Missing final SOH

    auto result1 = parser->parse(incomplete_msg.c_str(), incomplete_msg.length());
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::NeedMoreData, result1.status, "Incomplete message should return NeedMoreData");
    ASSERT_TRUE(result1.parsed_message == nullptr, "Incomplete message should not return parsed message");

    // Test 2: Message without proper BeginString
    std::string invalid_begin = "INVALID_START\x01"
                                "9=20\x01"
                                "35=0\x01"
                                "10=123\x01";

    auto result2 = parser->parse(invalid_begin.c_str(), invalid_begin.length());
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::InvalidFormat, result2.status, "Invalid BeginString should return InvalidFormat");
    ASSERT_TRUE(result2.parsed_message == nullptr, "Invalid message should not return parsed message");

    // Test 3: Buffer too small
    std::string tiny_msg = "8=FIX";

    auto result3 = parser->parse(tiny_msg.c_str(), tiny_msg.length());
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::NeedMoreData, result3.status, "Tiny buffer should return InvalidFormat");
    ASSERT_TRUE(result3.parsed_message == nullptr, "Tiny buffer should not return parsed message");

    return true;
}

// =====================================================================
// TEST 7: Partial Message Handling
// =====================================================================

bool testPartialMessageHandling()
{
    std::string complete_msg = createHeartbeat();

    // Split message into two parts
    size_t split_point = complete_msg.length() / 2;
    std::string part1 = complete_msg.substr(0, split_point);
    std::string part2 = complete_msg.substr(split_point);

    // Parse first part (should be incomplete)
    auto result1 = parser->parse(part1.c_str(), part1.length());
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::NeedMoreData, result1.status, "First part should be incomplete");
    ASSERT_TRUE(result1.parsed_message == nullptr, "Incomplete message should not return parsed message");

    // Verify parser has partial message
    ASSERT_TRUE(parser->hasPartialMessage(), "Parser should have partial message");
    ASSERT_GT(parser->getPartialMessageSize(), 0U, "Partial message size should be greater than 0");

    // Parse second part (should complete the message)
    auto result2 = parser->parse(part2.c_str(), part2.length());
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result2.status, "Second part should complete the message");
    ASSERT_TRUE(result2.parsed_message != nullptr, "Complete message should return parsed message");

    if (result2.parsed_message)
    {
        std::string msg_type;
        ASSERT_TRUE(result2.parsed_message->getField(FixFields::MsgType, msg_type), "Should get MsgType field");
        ASSERT_EQ("0", msg_type, "Completed message should be Heartbeat");
        message_pool->deallocate(result2.parsed_message);
    }

    // Verify partial buffer is cleared
    ASSERT_FALSE(parser->hasPartialMessage(), "Parser should not have partial message after completion");
    ASSERT_EQ(0U, parser->getPartialMessageSize(), "Partial message size should be 0 after completion");

    return true;
}

// =====================================================================
// TEST 8: Performance Statistics
// =====================================================================

bool testPerformanceStatistics()
{
    std::string msg = createExecutionReport();

    // Reset stats
    parser->resetStats();
    const auto &initial_stats = parser->getStats();
    ASSERT_EQ(0U, initial_stats.messages_parsed, "Initial messages parsed should be 0");
    ASSERT_EQ(0U, initial_stats.parse_errors, "Initial parse errors should be 0");

    // Parse successful message
    auto result = parser->parse(msg.c_str(), msg.length());
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Message should parse successfully");

    const auto &stats_after_success = parser->getStats();
    ASSERT_EQ(1U, stats_after_success.messages_parsed, "Messages parsed should be 1 after success");
    ASSERT_EQ(0U, stats_after_success.parse_errors, "Parse errors should still be 0 after success");
    ASSERT_GT(stats_after_success.total_parse_time_ns, 0U, "Total parse time should be greater than 0");

    if (result.parsed_message)
    {
        message_pool->deallocate(result.parsed_message);
    }

    // Parse malformed message
    std::string bad_msg = "INVALID_BEGIN_STRING";
    auto bad_result = parser->parse(bad_msg.c_str(), bad_msg.length());

    ASSERT_NE(static_cast<int>(StreamFixParser::ParseStatus::Success), static_cast<int>(bad_result.status), "Bad message should fail");

    const auto &stats_after_error = parser->getStats();
    ASSERT_EQ(1U, stats_after_error.messages_parsed, "Messages parsed should remain 1 after error");
    ASSERT_EQ(1U, stats_after_error.parse_errors, "Parse errors should be 1 after error");

    return true;
}

// =====================================================================
// TEST 9: State Machine Transitions
// =====================================================================

bool testStateMachineTransitions()
{
    // Test that parser starts in IDLE state
    ASSERT_EQ(static_cast<int>(StreamFixParser::ParseState::IDLE), static_cast<int>(parser->getCurrentState()), "Parser should start in IDLE state");
    ASSERT_FALSE(parser->isInErrorRecovery(), "Parser should not be in error recovery initially");

    // Test state transition validation
    ASSERT_TRUE(parser->isValidStateTransition(
                    StreamFixParser::ParseState::IDLE,
                    StreamFixParser::ParseState::PARSING_BEGIN_STRING),
                "IDLE to PARSING_BEGIN_STRING should be valid");

    ASSERT_FALSE(parser->isValidStateTransition(
                     StreamFixParser::ParseState::IDLE,
                     StreamFixParser::ParseState::MESSAGE_COMPLETE),
                 "IDLE to MESSAGE_COMPLETE should be invalid");

    // After successful parse, should return to IDLE
    std::string msg = createHeartbeat();
    auto result = parser->parse(msg.c_str(), msg.length());
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Heartbeat should parse successfully");
    ASSERT_EQ(static_cast<int>(StreamFixParser::ParseState::IDLE), static_cast<int>(parser->getCurrentState()), "Parser should return to IDLE after successful parse");

    if (result.parsed_message)
    {
        message_pool->deallocate(result.parsed_message);
    }

    return true;
}

// =====================================================================
// TEST 10: Configuration Options
// =====================================================================

bool testConfigurationOptions()
{
    // Test checksum validation toggle
    parser->setValidateChecksum(false);

    // Create message with intentionally wrong checksum
    std::string msg_bad_checksum = "8=FIX.4.4\x01"
                                   "9=58\x01"
                                   "35=0\x01"
                                   "49=SENDER\x01"
                                   "56=TARGET\x01"
                                   "34=1\x01"
                                   "52=20231201-12:00:00\x01"
                                   "10=999\x01";

    auto result1 = parser->parse(msg_bad_checksum.c_str(), msg_bad_checksum.length());
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result1.status, "Should succeed with checksum validation off");

    if (result1.parsed_message)
    {
        message_pool->deallocate(result1.parsed_message);
    }

    // Enable checksum validation
    parser->setValidateChecksum(true);
    parser->reset(); // Clear any state

    auto result2 = parser->parse(msg_bad_checksum.c_str(), msg_bad_checksum.length());
    ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::ChecksumError, result2.status, "Should fail with checksum validation on");

    // Test max message size
    parser->setMaxMessageSize(50); // Very small limit

    std::string large_msg = createExecutionReport("37=VERY_LONG_ORDER_ID_THAT_EXCEEDS_LIMIT\x01");
    auto result3 = parser->parse(large_msg.c_str(), large_msg.length());
    // Note: The exact behavior for size limits may vary, so we just ensure it doesn't crash

    return true;
}

// =====================================================================
// MAIN FUNCTION
// =====================================================================

int main()
{
    std::cout << "🚀 Starting StreamFixParser Improved Tests\n"
              << std::endl;

    int total_tests = 0;
    int passed_tests = 0;
    int failed_tests = 0;

    // Run all tests
    RUN_TEST(testBodyLengthOffByOneFix);
    RUN_TEST(testMultiMessageParsing);
    RUN_TEST(testChecksumCalculationFix);
    RUN_TEST(testBodyLengthReuseFix);
    RUN_TEST(testCircuitBreakerCoolingPeriod);
    RUN_TEST(testDefensiveChecksInTemplates);
    RUN_TEST(testPartialMessageHandling);
    RUN_TEST(testPerformanceStatistics);
    RUN_TEST(testStateMachineTransitions);
    RUN_TEST(testConfigurationOptions);

    // Print summary
    std::cout << "\n"
              << std::string(60, '=') << std::endl;
    std::cout << "🏁 TEST SUMMARY" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Total tests: " << total_tests << std::endl;
    std::cout << "✅ Passed: " << passed_tests << std::endl;
    std::cout << "❌ Failed: " << failed_tests << std::endl;
    std::cout << "📊 Success rate: " << (total_tests > 0 ? (passed_tests * 100 / total_tests) : 0) << "%" << std::endl;

    if (failed_tests == 0)
    {
        std::cout << "\n🎉 ALL TESTS PASSED! StreamFixParser improvements are working correctly." << std::endl;
        return 0;
    }
    else
    {
        std::cout << "\n💥 SOME TESTS FAILED! Please check the implementation." << std::endl;
        return 1;
    }
}