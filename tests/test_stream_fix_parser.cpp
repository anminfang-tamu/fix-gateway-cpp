#include "protocol/stream_fix_parser.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cassert>

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

class StreamFixParserTest
{
private:
    MessagePool<FixMessage> *message_pool_;
    std::unique_ptr<StreamFixParser> parser_;

public:
    StreamFixParserTest() : message_pool_(nullptr), parser_(nullptr) {}

    ~StreamFixParserTest()
    {
        cleanup();
    }

    bool setup()
    {
        try
        {
            // Create message pool with reasonable size for testing
            message_pool_ = new MessagePool<FixMessage>(1000, "test_pool");
            message_pool_->prewarm();

            // Create parser
            parser_ = std::make_unique<StreamFixParser>(message_pool_);

            std::cout << "✅ Test setup completed" << std::endl;
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
    // TEST DATA CREATION
    // =================================================================

    std::string createValidFixMessage(const std::string &msg_type = "D",
                                      const std::string &sender_comp_id = "SENDER",
                                      const std::string &target_comp_id = "TARGET")
    {
        // Create a valid FIX 4.4 message with correct structure and checksum
        std::string message = "8=FIX.4.4\x01"; // BeginString

        // Build body first to calculate BodyLength accurately
        std::string body = "35=" + msg_type + "\x01"; // MsgType
        body += "49=" + sender_comp_id + "\x01";      // SenderCompID
        body += "56=" + target_comp_id + "\x01";      // TargetCompID
        body += "52=20231201-12:00:00.000\x01";       // SendingTime
        body += "11=ORDER123\x01";                    // ClOrdID
        body += "55=AAPL\x01";                        // Symbol
        body += "54=1\x01";                           // Side (Buy)
        body += "38=100\x01";                         // OrderQty
        body += "44=150.25\x01";                      // Price
        body += "40=2\x01";                           // OrdType (Limit)
        body += "59=0\x01";                           // TimeInForce (Day)

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

        // Format checksum as 3-digit string with leading zeros
        char checksum_str[4];
        snprintf(checksum_str, sizeof(checksum_str), "%03d", checksum);
        message += "10=" + std::string(checksum_str) + "\x01";

        return message;
    }

    std::string createPartialFixMessage(const std::string &complete_message, size_t partial_length)
    {
        return complete_message.substr(0, std::min(partial_length, complete_message.length()));
    }

    std::string createCorruptedFixMessage()
    {
        return "8=FIX.4.4\x01"
               "9=INVALID\x01" // Invalid body length
               "35=D\x01"
               "10=123\x01";
    }

    void printMessage(const std::string &msg, const std::string &label = "Message")
    {
        std::cout << label << ": ";
        for (char c : msg)
        {
            if (c == '\x01')
            {
                std::cout << "<SOH>";
            }
            else
            {
                std::cout << c;
            }
        }
        std::cout << std::endl;
    }

    // =================================================================
    // BASIC PARSING TESTS
    // =================================================================

    bool testBasicParsing()
    {
        std::cout << "\n--- Testing Basic Parsing ---" << std::endl;

        // Test 1: Parse valid complete message
        std::string valid_message = createValidFixMessage();
        printMessage(valid_message, "Test message");

        auto result = parser_->parse(valid_message.data(), valid_message.length());

        std::cout << "Parse result status: " << static_cast<int>(result.status) << std::endl;
        std::cout << "Error detail: " << result.error_detail << std::endl;
        std::cout << "Bytes consumed: " << result.bytes_consumed << std::endl;

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Valid message should parse successfully");
        ASSERT_TRUE(result.parsed_message != nullptr, "Parsed message should not be null");

        // Verify message fields
        if (result.parsed_message)
        {
            std::string msg_type, symbol, side;
            ASSERT_TRUE(result.parsed_message->getField(FixFields::MsgType, msg_type), "Should have MsgType field");
            ASSERT_EQ(std::string("D"), msg_type, "MsgType should be 'D'");

            ASSERT_TRUE(result.parsed_message->getField(FixFields::Symbol, symbol), "Should have Symbol field");
            ASSERT_EQ(std::string("AAPL"), symbol, "Symbol should be 'AAPL'");

            ASSERT_TRUE(result.parsed_message->getField(FixFields::Side, side), "Should have Side field");
            ASSERT_EQ(std::string("1"), side, "Side should be '1'");

            std::cout << "✅ Parsed fields verified: MsgType=" << msg_type << ", Symbol=" << symbol << ", Side=" << side << std::endl;

            // Return message to pool
            message_pool_->deallocate(result.parsed_message);
        }

        // Test 2: Parse empty buffer
        result = parser_->parse(nullptr, 0);
        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::InvalidFormat, result.status, "Empty buffer should return InvalidFormat");

        std::cout << "✅ Basic parsing tests passed" << std::endl;
        return true;
    }

    // =================================================================
    // STATE MACHINE TESTS
    // =================================================================

    bool testStateMachineTransitions()
    {
        std::cout << "\n--- Testing State Machine Transitions ---" << std::endl;

        // Reset parser to ensure clean state
        parser_->reset();
        ASSERT_EQ_STATUS(StreamFixParser::ParseState::IDLE, parser_->getCurrentState(), "Parser should start in IDLE state");

        // Test valid state transitions
        ASSERT_TRUE(parser_->isValidStateTransition(StreamFixParser::ParseState::IDLE,
                                                    StreamFixParser::ParseState::PARSING_BEGIN_STRING),
                    "IDLE to PARSING_BEGIN_STRING should be valid");

        ASSERT_FALSE(parser_->isValidStateTransition(StreamFixParser::ParseState::IDLE,
                                                     StreamFixParser::ParseState::MESSAGE_COMPLETE),
                     "IDLE to MESSAGE_COMPLETE should be invalid");

        std::cout << "✅ State machine transition tests passed" << std::endl;
        return true;
    }

    // =================================================================
    // PARTIAL MESSAGE HANDLING TESTS
    // =================================================================

    bool testPartialMessageHandling()
    {
        std::cout << "\n--- Testing Partial Message Handling ---" << std::endl;

        parser_->reset();
        std::string complete_message = createValidFixMessage();
        printMessage(complete_message, "Complete message");

        // Test 1: Parse in small chunks
        size_t chunk_size = 8;
        StreamFixParser::ParseResult final_result;
        bool parsing_complete = false;

        for (size_t pos = 0; pos < complete_message.length() && !parsing_complete; pos += chunk_size)
        {
            size_t remaining = complete_message.length() - pos;
            size_t current_chunk = std::min(chunk_size, remaining);

            std::cout << "Parsing chunk " << (pos / chunk_size + 1) << " (bytes " << pos << "-" << (pos + current_chunk - 1) << ")" << std::endl;

            auto result = parser_->parse(complete_message.data() + pos, current_chunk);

            std::cout << "  Status: " << static_cast<int>(result.status) << ", Consumed: " << result.bytes_consumed << std::endl;
            std::cout << "  Current state: " << static_cast<int>(parser_->getCurrentState()) << std::endl;

            if (result.status == StreamFixParser::ParseStatus::Success)
            {
                final_result = result;
                parsing_complete = true;
                std::cout << "  ✅ Parsing completed successfully!" << std::endl;
            }
            else if (result.status == StreamFixParser::ParseStatus::NeedMoreData)
            {
                std::cout << "  ⏳ Need more data, continuing..." << std::endl;
                // Continue parsing - adjust position based on consumed bytes
                if (result.bytes_consumed > 0)
                {
                    pos += result.bytes_consumed - chunk_size; // Adjust for the consumed bytes
                }
            }
            else
            {
                std::cout << "  ❌ Unexpected status: " << result.error_detail << std::endl;
                ASSERT_TRUE(false, "Unexpected parse status during progressive parsing: " + result.error_detail);
            }
        }

        ASSERT_TRUE(parsing_complete, "Progressive parsing should complete");
        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, final_result.status, "Progressive parsing should succeed");
        ASSERT_TRUE(final_result.parsed_message != nullptr, "Should have final parsed message");

        if (final_result.parsed_message)
        {
            message_pool_->deallocate(final_result.parsed_message);
        }

        std::cout << "✅ Partial message handling tests passed" << std::endl;
        return true;
    }

    // =================================================================
    // ERROR HANDLING AND RECOVERY TESTS
    // =================================================================

    bool testErrorHandlingAndRecovery()
    {
        std::cout << "\n--- Testing Error Handling and Recovery ---" << std::endl;

        parser_->reset();

        // Test 1: Corrupted data
        std::string corrupted = createCorruptedFixMessage();
        printMessage(corrupted, "Corrupted message");

        auto result = parser_->parse(corrupted.data(), corrupted.length());

        std::cout << "Corrupted message result: " << static_cast<int>(result.status) << std::endl;
        std::cout << "Error detail: " << result.error_detail << std::endl;

        ASSERT_TRUE(result.status != StreamFixParser::ParseStatus::Success, "Corrupted message should fail");

        // Test 2: Recovery with valid message after error
        parser_->reset();
        std::string valid_message = createValidFixMessage();
        result = parser_->parse(valid_message.data(), valid_message.length());

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Should recover and parse valid message");
        if (result.parsed_message)
        {
            message_pool_->deallocate(result.parsed_message);
        }

        std::cout << "✅ Error handling and recovery tests passed" << std::endl;
        return true;
    }

    // =================================================================
    // MULTIPLE MESSAGE PARSING TESTS
    // =================================================================

    bool testMultipleMessageParsing()
    {
        std::cout << "\n--- Testing Multiple Message Parsing ---" << std::endl;

        // Create different message types
        std::string msg1 = createValidFixMessage("D", "SENDER1", "TARGET1"); // New Order Single
        std::string msg2 = createValidFixMessage("8", "SENDER2", "TARGET2"); // Execution Report
        std::string msg3 = createValidFixMessage("G", "SENDER3", "TARGET3"); // Order Cancel Replace

        std::vector<std::string> messages = {msg1, msg2, msg3};
        std::vector<std::string> expected_types = {"D", "8", "G"};

        // Parse each message individually
        for (size_t i = 0; i < messages.size(); i++)
        {
            parser_->reset(); // Reset for each message
            std::cout << "Parsing message " << (i + 1) << " (type " << expected_types[i] << ")" << std::endl;

            auto result = parser_->parse(messages[i].data(), messages[i].length());

            ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status,
                             "Message " + std::to_string(i + 1) + " should parse successfully");
            ASSERT_TRUE(result.parsed_message != nullptr,
                        "Message " + std::to_string(i + 1) + " should not be null");

            // Verify message type
            if (result.parsed_message)
            {
                std::string msg_type;
                ASSERT_TRUE(result.parsed_message->getField(FixFields::MsgType, msg_type),
                            "Message should have MsgType");
                ASSERT_EQ(expected_types[i], msg_type, "Message type should match expected");

                std::cout << "  ✅ Message " << (i + 1) << " parsed with type: " << msg_type << std::endl;

                // Return to pool
                message_pool_->deallocate(result.parsed_message);
            }
        }

        std::cout << "✅ Multiple message parsing tests passed" << std::endl;
        return true;
    }

    // =================================================================
    // PERFORMANCE AND STATISTICS TESTS
    // =================================================================

    bool testPerformanceAndStatistics()
    {
        std::cout << "\n--- Testing Performance and Statistics ---" << std::endl;

        parser_->reset();
        parser_->resetStats();

        // Parse multiple messages to gather statistics
        std::string test_message = createValidFixMessage();
        const int num_messages = 50;

        auto start_time = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < num_messages; i++)
        {
            auto result = parser_->parse(test_message.data(), test_message.length());
            ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Message should parse");

            if (result.parsed_message)
            {
                message_pool_->deallocate(result.parsed_message);
            }

            parser_->reset(); // Reset for next message
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        // Check statistics
        auto stats = parser_->getStats();
        ASSERT_EQ(static_cast<uint64_t>(num_messages), stats.messages_parsed, "Should have parsed all messages");
        ASSERT_TRUE(stats.total_parse_time_ns > 0, "Should have recorded parse time");

        std::cout << "Performance Results:" << std::endl;
        std::cout << "  Messages parsed: " << stats.messages_parsed << std::endl;
        std::cout << "  Total time: " << duration.count() << " microseconds" << std::endl;
        std::cout << "  Average per message: " << (duration.count() / num_messages) << " microseconds" << std::endl;
        std::cout << "  Average parse time (ns): " << stats.getAverageParseTimeNs() << std::endl;
        std::cout << "  State transitions: " << stats.state_transitions << std::endl;

        std::cout << "✅ Performance and statistics tests passed" << std::endl;
        return true;
    }

    // =================================================================
    // CHECKSUM VALIDATION TESTS
    // =================================================================

    bool testChecksumValidation()
    {
        std::cout << "\n--- Testing Checksum Validation ---" << std::endl;

        parser_->reset();
        parser_->setValidateChecksum(true);

        // Test 1: Valid checksum
        std::string valid_message = createValidFixMessage();
        auto result = parser_->parse(valid_message.data(), valid_message.length());

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Valid checksum should pass");
        if (result.parsed_message)
        {
            message_pool_->deallocate(result.parsed_message);
        }

        // Test 2: Disable checksum validation
        parser_->reset();
        parser_->setValidateChecksum(false);
        result = parser_->parse(valid_message.data(), valid_message.length());
        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Should pass with checksum validation disabled");

        if (result.parsed_message)
        {
            message_pool_->deallocate(result.parsed_message);
        }

        std::cout << "✅ Checksum validation tests passed" << std::endl;
        return true;
    }

    // =================================================================
    // CONFIGURATION TESTS
    // =================================================================

    bool testConfiguration()
    {
        std::cout << "\n--- Testing Configuration ---" << std::endl;

        // Test configuration changes
        parser_->setMaxMessageSize(16384); // 16KB
        parser_->setValidateChecksum(true);
        parser_->setStrictValidation(true);
        parser_->setMaxConsecutiveErrors(5);
        parser_->setErrorRecoveryEnabled(true);

        // Create and parse a valid message with new configuration
        std::string test_message = createValidFixMessage();
        auto result = parser_->parse(test_message.data(), test_message.length());

        ASSERT_EQ_STATUS(StreamFixParser::ParseStatus::Success, result.status, "Should parse with new configuration");

        if (result.parsed_message)
        {
            message_pool_->deallocate(result.parsed_message);
        }

        std::cout << "✅ Configuration tests passed" << std::endl;
        return true;
    }

    // =================================================================
    // MAIN TEST RUNNER
    // =================================================================

    bool runAllTests()
    {
        std::cout << "=== StreamFixParser State Machine Test Suite ===" << std::endl;
        std::cout << "Testing the complete FIX message parser implementation" << std::endl;
        std::cout << "=================================================" << std::endl;

        if (!setup())
        {
            return false;
        }

        bool all_passed = true;

        try
        {
            all_passed &= testBasicParsing();
            all_passed &= testStateMachineTransitions();
            all_passed &= testPartialMessageHandling();
            all_passed &= testErrorHandlingAndRecovery();
            all_passed &= testMultipleMessageParsing();
            all_passed &= testPerformanceAndStatistics();
            all_passed &= testChecksumValidation();
            all_passed &= testConfiguration();
        }
        catch (const std::exception &e)
        {
            std::cout << "❌ Test suite failed with exception: " << e.what() << std::endl;
            all_passed = false;
        }

        cleanup();

        std::cout << "\n=================================================" << std::endl;
        if (all_passed)
        {
            std::cout << "🎉 ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
            std::cout << "✅ State machine parser is working correctly" << std::endl;
        }
        else
        {
            std::cout << "❌ SOME TESTS FAILED!" << std::endl;
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
    StreamFixParserTest test_suite;

    bool success = test_suite.runAllTests();

    return success ? 0 : 1;
}