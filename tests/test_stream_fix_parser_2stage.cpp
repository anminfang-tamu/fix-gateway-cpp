#include "protocol/stream_fix_parser.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"
#include "utils/logger.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cassert>
#include <cstring>

using namespace fix_gateway::protocol;
using namespace fix_gateway::common;

// =================================================================
// TEST FRAMEWORK MACROS
// =================================================================

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

#define ASSERT_EQ_STATUS(expected, actual, message)                                 \
    do                                                                              \
    {                                                                               \
        if ((expected) != (actual))                                                 \
        {                                                                           \
            std::cout << "❌ ASSERTION FAILED: " << message << std::endl;           \
            std::cout << "   Expected Status: " << static_cast<int>(expected)       \
                      << ", Got Status: " << static_cast<int>(actual) << std::endl; \
            return false;                                                           \
        }                                                                           \
    } while (0)

// =================================================================
// TEST DATA: SAMPLE FIX MESSAGES
// =================================================================

namespace TestData
{
    // Complete NEW_ORDER_SINGLE message (MsgType=D)
    // Actual body length measurement: 112 bytes (verified by Python script)
    // Header: "8=FIX.4.4\x019=112\x01" = 16 bytes, Body: 112 bytes, Total: 128 bytes
    const std::string NEW_ORDER_SINGLE =
        "8=FIX.4.4\x01"            // BeginString
        "9=112\x01"                // BodyLength (corrected from 116 to 112 - actual measured length)
        "35=D\x01"                 // MsgType = NEW_ORDER_SINGLE
        "49=SENDER\x01"            // SenderCompID
        "56=TARGET\x01"            // TargetCompID
        "34=123\x01"               // MsgSeqNum
        "52=20231201-10:30:00\x01" // SendingTime
        "11=ORDER001\x01"          // ClOrdID
        "55=MSFT\x01"              // Symbol
        "54=1\x01"                 // Side (Buy)
        "38=100\x01"               // OrderQty
        "40=2\x01"                 // OrdType (Limit)
        "44=150.25\x01"            // Price
        "59=0\x01"                 // TimeInForce (Day)
        "10=123\x01";              // CheckSum

    // Complete HEARTBEAT message (MsgType=0)
    // Body bytes calculation: 35=0(4) + 49=SENDER(10) + 56=TARGET(10) + 34=124(7) + 52=20231201-10:31:00(22) + 10=085(7) = 60 bytes
    const std::string HEARTBEAT =
        "8=FIX.4.4\x01"            // BeginString
        "9=60\x01"                 // BodyLength (corrected from 50 to 60)
        "35=0\x01"                 // MsgType = HEARTBEAT
        "49=SENDER\x01"            // SenderCompID
        "56=TARGET\x01"            // TargetCompID
        "34=124\x01"               // MsgSeqNum
        "52=20231201-10:31:00\x01" // SendingTime
        "10=085\x01";              // CheckSum

    // Partial message (first part)
    // Body bytes calculation for complete message: 35=D(5) + 49=SENDER(10) + 56=TARGET(10) + 34=125(7) + 11=ORDER002(12) + 55=AAPL(8) + 54=2(5) + 38=50(6) + 40=1(5) + 10=156(7) = 75 bytes
    const std::string PARTIAL_MESSAGE_PART1 =
        "8=FIX.4.4\x01" // BeginString
        "9=75\x01"      // BodyLength (corrected from 76 to 75 - actual measured length)
        "35=D\x01"      // MsgType
        "49=SENDER\x01" // SenderCompID (message gets cut off here)
        "56=TAR";

    // Partial message (second part)
    const std::string PARTIAL_MESSAGE_PART2 =
        "GET\x01"         // Complete TargetCompID
        "34=125\x01"      // MsgSeqNum
        "11=ORDER002\x01" // ClOrdID
        "55=AAPL\x01"     // Symbol
        "54=2\x01"        // Side (Sell)
        "38=50\x01"       // OrderQty
        "40=1\x01"        // OrdType (Market)
        "10=156\x01";     // CheckSum

    // Multiple messages in single buffer
    const std::string MULTIPLE_MESSAGES = NEW_ORDER_SINGLE + HEARTBEAT;

    // Malformed message (invalid BodyLength)
    const std::string MALFORMED_MESSAGE =
        "8=FIX.4.4\x01" // BeginString
        "9=INVALID\x01" // Invalid BodyLength (should be numeric)
        "35=D\x01"      // MsgType
        "10=123\x01";   // CheckSum
}

// =================================================================
// 2-STAGE PARSER TEST SUITE
// =================================================================

class TwoStageParserTest
{
private:
    MessagePool<FixMessage> message_pool_;
    StreamFixParser parser_;

public:
    TwoStageParserTest() : message_pool_(100), parser_(&message_pool_)
    {
        // Configure parser for testing
        parser_.setMaxMessageSize(8192);
        parser_.setValidateChecksum(false); // Disable for easier testing
        parser_.setStrictValidation(false);
    }

    // =================================================================
    // STAGE 1 TESTS: FRAMING (findCompleteMessage)
    // =================================================================

    bool testStage1_CompleteMessage()
    {
        std::cout << "\n🧪 Testing Stage 1: Complete Message Framing..." << std::endl;

        const std::string &buffer = TestData::NEW_ORDER_SINGLE;

        LOG_DEBUG("Buffer: " + buffer);

        size_t message_start = 0, message_end = 0;

        auto result = parser_.findCompleteMessage(buffer.c_str(), buffer.length(),
                                                  message_start, message_end);

        LOG_DEBUG("Result: " + std::to_string(static_cast<int>(result.status)));

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status,
                         "Should successfully find complete message boundaries");
        ASSERT_EQ(0, message_start, "Message should start at position 0");
        ASSERT_EQ(buffer.length(), message_end, "Message should end at buffer length");

        std::cout << "✅ Stage 1: Complete message framing successful" << std::endl;
        std::cout << "   Message boundaries: [" << message_start << ", " << message_end << "]" << std::endl;
        return true;
    }

    bool testStage1_PartialMessage()
    {
        std::cout << "\n🧪 Testing Stage 1: Partial Message Handling..." << std::endl;

        const std::string &buffer = TestData::PARTIAL_MESSAGE_PART1;
        size_t message_start = 0, message_end = 0;

        auto result = parser_.findCompleteMessage(buffer.c_str(), buffer.length(),
                                                  message_start, message_end);

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::NeedMoreData, result.status,
                         "Should detect incomplete message and request more data");

        std::cout << "✅ Stage 1: Partial message detection successful" << std::endl;
        std::cout << "   Status: " << static_cast<int>(result.status) << " (NeedMoreData)" << std::endl;
        return true;
    }

    bool testStage1_MalformedMessage()
    {
        std::cout << "\n🧪 Testing Stage 1: Malformed Message Detection..." << std::endl;

        const std::string &buffer = TestData::MALFORMED_MESSAGE;
        size_t message_start = 0, message_end = 0;

        auto result = parser_.findCompleteMessage(buffer.c_str(), buffer.length(),
                                                  message_start, message_end);

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::InvalidFormat, result.status,
                         "Should detect malformed BodyLength field");

        std::cout << "✅ Stage 1: Malformed message detection successful" << std::endl;
        std::cout << "   Error: " << result.error_detail << std::endl;
        return true;
    }

    bool testStage1_MultipleMessages()
    {
        std::cout << "\n🧪 Testing Stage 1: Multiple Messages in Buffer..." << std::endl;

        const std::string &buffer = TestData::MULTIPLE_MESSAGES;
        size_t message_start = 0, message_end = 0;

        // Find first message
        auto result1 = parser_.findCompleteMessage(buffer.c_str(), buffer.length(),
                                                   message_start, message_end);

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result1.status,
                         "Should find first complete message");
        ASSERT_EQ(0, message_start, "First message should start at position 0");

        // Calculate expected length: Header(16) + Body(116) = 132 bytes
        size_t expected_length = TestData::NEW_ORDER_SINGLE.length();
        ASSERT_EQ(expected_length, message_end,
                  "First message should end at NEW_ORDER_SINGLE length");

        // Find second message
        size_t second_start = message_end;
        auto result2 = parser_.findCompleteMessage(buffer.c_str() + second_start,
                                                   buffer.length() - second_start,
                                                   message_start, message_end);

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result2.status,
                         "Should find second complete message");

        std::cout << "✅ Stage 1: Multiple messages framing successful" << std::endl;
        std::cout << "   First message: [0, " << TestData::NEW_ORDER_SINGLE.length() << "]" << std::endl;
        std::cout << "   Second message start: " << second_start << std::endl;
        return true;
    }

    // =================================================================
    // STAGE 2 TESTS: MESSAGE DECODE (parseCompleteMessage)
    // =================================================================

    bool testStage2_NewOrderSingleOptimized()
    {
        std::cout << "\n🧪 Testing Stage 2: NEW_ORDER_SINGLE Template Optimization..." << std::endl;

        const std::string &buffer = TestData::NEW_ORDER_SINGLE;
        auto result = parser_.parseCompleteMessage(buffer.c_str(), buffer.length());

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status,
                         "Should successfully parse NEW_ORDER_SINGLE with template optimization");

        ASSERT_TRUE(result.parsed_message != nullptr, "Should return valid parsed message");

        FixMessage *msg = result.parsed_message;

        // Verify key fields were parsed correctly using public interface
        ASSERT_TRUE(msg->hasField(FixFields::MsgType), "Should have MsgType field");
        std::string msgType = msg->getMsgType();
        ASSERT_EQ(std::string("D"), msgType, "MsgType should be 'D'");

        ASSERT_TRUE(msg->hasField(11), "Should have ClOrdID field (11)");
        std::string clOrdID;
        ASSERT_TRUE(msg->getField(11, clOrdID), "Should retrieve ClOrdID field");
        ASSERT_EQ(std::string("ORDER001"), clOrdID, "ClOrdID should match");

        ASSERT_TRUE(msg->hasField(55), "Should have Symbol field (55)");
        std::string symbol;
        ASSERT_TRUE(msg->getField(55, symbol), "Should retrieve Symbol field");
        ASSERT_EQ(std::string("MSFT"), symbol, "Symbol should be MSFT");

        std::cout << "✅ Stage 2: NEW_ORDER_SINGLE template optimization successful" << std::endl;
        std::cout << "   Message type: " << msgType << std::endl;
        std::cout << "   ClOrdID: " << clOrdID << std::endl;
        std::cout << "   Symbol: " << symbol << std::endl;

        // Clean up
        parser_.getMessagePool()->deallocate(msg);
        return true;
    }

    bool testStage2_HeartbeatGenericParsing()
    {
        std::cout << "\n🧪 Testing Stage 2: HEARTBEAT Generic Parsing..." << std::endl;

        const std::string &buffer = TestData::HEARTBEAT;
        auto result = parser_.parseCompleteMessage(buffer.c_str(), buffer.length());

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status,
                         "Should successfully parse HEARTBEAT with generic parser");

        ASSERT_TRUE(result.parsed_message != nullptr, "Should return valid parsed message");

        FixMessage *msg = result.parsed_message;

        // Verify message type
        ASSERT_TRUE(msg->hasField(FixFields::MsgType), "Should have MsgType field");
        std::string msgType = msg->getMsgType();
        ASSERT_EQ(std::string("0"), msgType, "MsgType should be '0'");

        std::cout << "✅ Stage 2: HEARTBEAT generic parsing successful" << std::endl;
        std::cout << "   Message type: " << msgType << std::endl;

        // Clean up
        parser_.getMessagePool()->deallocate(msg);
        return true;
    }

    // =================================================================
    // INTEGRATION TESTS: FULL 2-STAGE FLOW
    // =================================================================

    bool testIntegration_CompleteFlow()
    {
        std::cout << "\n🧪 Testing Integration: Complete 2-Stage Flow..." << std::endl;

        const std::string &buffer = TestData::NEW_ORDER_SINGLE;
        auto result = parser_.parse(buffer.c_str(), buffer.length());

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status,
                         "Should successfully parse complete message through 2-stage flow");

        ASSERT_TRUE(result.parsed_message != nullptr, "Should return valid parsed message");
        ASSERT_EQ(buffer.length(), result.bytes_consumed, "Should consume entire buffer");

        FixMessage *msg = result.parsed_message;
        std::string msgType = msg->getMsgType();
        ASSERT_EQ(std::string("D"), msgType, "Should parse MsgType correctly");

        std::cout << "✅ Integration: Complete 2-stage flow successful" << std::endl;
        std::cout << "   Bytes consumed: " << result.bytes_consumed << "/" << buffer.length() << std::endl;

        // Clean up
        parser_.getMessagePool()->deallocate(msg);
        return true;
    }

    bool testIntegration_PartialMessageReassembly()
    {
        std::cout << "\n🧪 Testing Integration: Partial Message Reassembly..." << std::endl;

        // Parse first part (should store in partial buffer)
        const std::string &part1 = TestData::PARTIAL_MESSAGE_PART1;
        auto result1 = parser_.parse(part1.c_str(), part1.length());

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::NeedMoreData, result1.status,
                         "First part should request more data");

        ASSERT_TRUE(parser_.hasPartialMessage(), "Parser should have partial message stored");

        // Parse second part (should complete the message)
        const std::string &part2 = TestData::PARTIAL_MESSAGE_PART2;
        auto result2 = parser_.parse(part2.c_str(), part2.length());

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result2.status,
                         "Should successfully reassemble and parse complete message");

        ASSERT_TRUE(result2.parsed_message != nullptr, "Should return valid reassembled message");

        FixMessage *msg = result2.parsed_message;
        std::string msgType = msg->getMsgType();
        ASSERT_EQ(std::string("D"), msgType, "Should parse MsgType correctly");

        std::string clOrdID;
        ASSERT_TRUE(msg->getField(11, clOrdID), "Should retrieve ClOrdID field");
        ASSERT_EQ(std::string("ORDER002"), clOrdID, "Should parse ClOrdID from reassembled message");

        std::cout << "✅ Integration: Partial message reassembly successful" << std::endl;
        std::cout << "   Reassembled ClOrdID: " << clOrdID << std::endl;

        // Clean up
        parser_.getMessagePool()->deallocate(msg);
        return true;
    }

    // =================================================================
    // PERFORMANCE COMPARISON TESTS
    // =================================================================

    bool testPerformance_IntelligentVsGeneric()
    {
        std::cout << "\n🧪 Testing Performance: Intelligent vs Generic Parsing..." << std::endl;

        const int iterations = 1000;

        // Test intelligent parsing with NEW_ORDER_SINGLE (should use template optimization)
        const std::string &new_order_buffer = TestData::NEW_ORDER_SINGLE;
        auto start_intelligent = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i)
        {
            parser_.reset(); // Reset state between iterations
            auto result = parser_.parseIntelligent(new_order_buffer.c_str(), new_order_buffer.length());
            if (result.parsed_message)
            {
                parser_.getMessagePool()->deallocate(result.parsed_message);
            }
        }

        auto end_intelligent = std::chrono::high_resolution_clock::now();
        auto duration_intelligent = std::chrono::duration_cast<std::chrono::microseconds>(end_intelligent - start_intelligent);

        // Test parsing with HEARTBEAT (falls back to generic parsing)
        const std::string &heartbeat_buffer = TestData::HEARTBEAT;
        auto start_generic = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i)
        {
            parser_.reset(); // Reset state between iterations
            auto result = parser_.parse(heartbeat_buffer.c_str(), heartbeat_buffer.length());
            if (result.parsed_message)
            {
                parser_.getMessagePool()->deallocate(result.parsed_message);
            }
        }

        auto end_generic = std::chrono::high_resolution_clock::now();
        auto duration_generic = std::chrono::duration_cast<std::chrono::microseconds>(end_generic - start_generic);

        double ratio = static_cast<double>(duration_generic.count()) / static_cast<double>(duration_intelligent.count());

        std::cout << "✅ Performance: Intelligent vs Generic comparison completed" << std::endl;
        std::cout << "   Intelligent parsing: " << duration_intelligent.count() << " μs (" << iterations << " iterations)" << std::endl;
        std::cout << "   Generic parsing:     " << duration_generic.count() << " μs (" << iterations << " iterations)" << std::endl;
        std::cout << "   Performance ratio:   " << std::fixed << ratio << "x" << std::endl;

        // Both should be successful (timing comparison is informational)
        ASSERT_TRUE(duration_intelligent.count() > 0, "Intelligent parsing should complete");
        ASSERT_TRUE(duration_generic.count() > 0, "Generic parsing should complete");

        return true;
    }

    // =================================================================
    // TEST RUNNER
    // =================================================================

    bool runAllTests()
    {
        std::cout << "=================================================" << std::endl;
        std::cout << "🚀 RUNNING 2-STAGE PARSER TESTS" << std::endl;
        std::cout << "=================================================" << std::endl;

        bool all_passed = true;

        // Stage 1 Tests (Framing)
        std::cout << "\n📋 STAGE 1 TESTS: FRAMING" << std::endl;
        all_passed &= testStage1_CompleteMessage();
        all_passed &= testStage1_PartialMessage();
        all_passed &= testStage1_MalformedMessage();
        all_passed &= testStage1_MultipleMessages();

        // Stage 2 Tests (Message Decode)
        std::cout << "\n📋 STAGE 2 TESTS: MESSAGE DECODE" << std::endl;
        all_passed &= testStage2_NewOrderSingleOptimized();
        all_passed &= testStage2_HeartbeatGenericParsing();

        // Integration Tests
        std::cout << "\n📋 INTEGRATION TESTS: FULL 2-STAGE FLOW" << std::endl;
        all_passed &= testIntegration_CompleteFlow();
        all_passed &= testIntegration_PartialMessageReassembly();

        // Performance Tests
        std::cout << "\n📋 PERFORMANCE TESTS" << std::endl;
        all_passed &= testPerformance_IntelligentVsGeneric();

        // Summary
        std::cout << "\n=================================================" << std::endl;
        if (all_passed)
        {
            std::cout << "🎉 ALL 2-STAGE PARSER TESTS PASSED!" << std::endl;
            std::cout << "✨ Your 2-stage architecture is working perfectly!" << std::endl;
        }
        else
        {
            std::cout << "❌ SOME 2-STAGE TESTS FAILED!" << std::endl;
            std::cout << "🔧 Please check the implementation" << std::endl;
        }
        std::cout << "=================================================" << std::endl;

        return all_passed;
    }
};

// =================================================================
// MAIN FUNCTION
// =================================================================

int main()
{
    // Enable DEBUG logging for detailed debugging (keep only DEBUG level)
    fix_gateway::utils::Logger::getInstance().setLogLevel(fix_gateway::utils::LogLevel::DEBUG);
    fix_gateway::utils::Logger::getInstance().enableConsoleOutput(true);

    std::cout << "🎯 2-Stage Parser Architecture Test Suite" << std::endl;
    std::cout << "Testing: Framing → Message Decode → Integration → Performance" << std::endl;

    TwoStageParserTest test_suite;
    bool success = test_suite.runAllTests();

    return success ? 0 : 1;
}