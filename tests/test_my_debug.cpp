#include "protocol/stream_fix_parser.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"
#include <iostream>
#include <string>

using namespace fix_gateway::protocol;
using namespace fix_gateway::common;

std::string printMessage(const std::string &msg, const std::string &label = "Message")
{
    std::string result = label + ": ";
    for (char c : msg)
    {
        if (c == '\x01')
        {
            result += "<SOH>";
        }
        else
        {
            result += c;
        }
    }
    return result;
}

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

int main()
{
    std::cout << "=== My Simple Debug Test ===" << std::endl;

    // Create message pool
    MessagePool<FixMessage> message_pool(100, "debug_pool");
    message_pool.prewarm();

    // Create parser
    StreamFixParser parser(&message_pool);

    // Create test message
    std::string test_message = createValidFixMessage();
    std::cout << printMessage(test_message, "Test Message") << std::endl;
    std::cout << "Message length: " << test_message.length() << std::endl;

    // Show what each part of the message looks like
    std::cout << "\nMessage breakdown:" << std::endl;
    std::cout << "BeginString: 8=FIX.4.4<SOH> (length: " << strlen("8=FIX.4.4") + 1 << ")" << std::endl;

    // Find body length field position
    size_t soh_pos = test_message.find('\x01');
    std::string body_length_field = test_message.substr(soh_pos + 1, test_message.find('\x01', soh_pos + 1) - soh_pos - 1);
    std::cout << "BodyLength: " << body_length_field << "<SOH> (starts at position " << (soh_pos + 1) << ")" << std::endl;

    // Test parsing step by step
    std::cout << "\n=== Parsing Test ===" << std::endl;

    // Get initial state
    std::cout << "Initial state: " << static_cast<int>(parser.getCurrentState()) << std::endl;

    // Try to parse
    auto result = parser.parse(test_message.data(), test_message.length());

    std::cout << "Parse result:" << std::endl;
    std::cout << "  Status: " << static_cast<int>(result.status) << std::endl;
    std::cout << "  Error detail: " << result.error_detail << std::endl;
    std::cout << "  Bytes consumed: " << result.bytes_consumed << std::endl;
    std::cout << "  Final state: " << static_cast<int>(result.final_state) << std::endl;
    std::cout << "  Error position: " << result.error_position << std::endl;

    if (result.parsed_message)
    {
        std::cout << "Message successfully parsed!" << std::endl;
        message_pool.deallocate(result.parsed_message);
    }

    return 0;
}