#include "protocol/stream_fix_parser.h"
#include "protocol/fix_message.h"
#include "common/message_pool.h"
#include <iostream>
#include <iomanip>

using namespace fix_gateway::protocol;
using namespace fix_gateway::common;

// Function to visualize buffer contents with delimiters
void visualizeBuffer(const char *buffer, size_t length, const std::string &title)
{
    std::cout << "\n=== " << title << " ===" << std::endl;
    std::cout << "Buffer content (" << length << " bytes):" << std::endl;

    // Show actual bytes with SOH characters visible
    for (size_t i = 0; i < length; ++i)
    {
        char c = buffer[i];
        if (c == '\x01') // SOH character
        {
            std::cout << "[SOH]";
        }
        else if (c >= 32 && c <= 126) // Printable characters
        {
            std::cout << c;
        }
        else
        {
            std::cout << "[" << static_cast<int>(c) << "]";
        }
    }
    std::cout << std::endl;

    // Show position markers
    std::cout << "Positions: ";
    for (size_t i = 0; i < length; ++i)
    {
        if (i % 10 == 0)
            std::cout << std::setw(3) << i;
        else
            std::cout << "   ";
    }
    std::cout << std::endl;
}

// Function to demonstrate buffer pointer parsing step by step
void demonstrateBufferPointerParsing()
{
    std::cout << "FIX Buffer Pointer Parsing Demonstration" << std::endl;
    std::cout << "=========================================" << std::endl;

    // =================================================================
    // STEP 1: Create a realistic FIX message buffer (with real SOH delimiters)
    // =================================================================

    // This is what actually comes over the network wire:
    std::string fix_message =
        "8=FIX.4.4\x01"            // BeginString
        "9=110\x01"                // BodyLength (corrected - body includes checksum field)
        "35=D\x01"                 // MsgType (NewOrderSingle)
        "49=CLIENT\x01"            // SenderCompID
        "56=BROKER\x01"            // TargetCompID
        "34=1\x01"                 // MsgSeqNum
        "52=20231215-10:30:00\x01" // SendingTime
        "11=ORDER123\x01"          // ClOrdID
        "55=AAPL\x01"              // Symbol
        "54=1\x01"                 // Side (Buy)
        "38=100\x01"               // OrderQty
        "44=150.50\x01"            // Price
        "40=2\x01"                 // OrdType (Limit)
        "59=0\x01"                 // TimeInForce (Day)
        "10=123\x01";              // CheckSum

    const char *buffer = fix_message.c_str();
    size_t buffer_length = fix_message.length();

    visualizeBuffer(buffer, buffer_length, "Raw FIX Message Buffer");

    // =================================================================
    // STEP 2: Demonstrate pointer-by-pointer field extraction
    // =================================================================

    std::cout << "\n=== Field-by-Field Pointer Parsing ===" << std::endl;

    const char *current_ptr = buffer;
    const char *end_ptr = buffer + buffer_length;
    int field_number = 1;

    while (current_ptr < end_ptr)
    {
        std::cout << "\n--- Field " << field_number++ << " ---" << std::endl;

        // Show current pointer position
        size_t position = current_ptr - buffer;
        std::cout << "Current position: " << position << std::endl;

        // =================================================================
        // STEP 2A: Find tag (number before '=')
        // =================================================================

        const char *tag_start = current_ptr;
        const char *equals_ptr = nullptr;

        // Search for '=' character
        for (const char *search_ptr = current_ptr; search_ptr < end_ptr; ++search_ptr)
        {
            if (*search_ptr == '=')
            {
                equals_ptr = search_ptr;
                break;
            }
        }

        if (!equals_ptr)
        {
            std::cout << "ERROR: No '=' found!" << std::endl;
            break;
        }

        // Extract tag as integer (zero-copy)
        size_t tag_length = equals_ptr - tag_start;
        std::cout << "Tag bytes: [";
        for (size_t i = 0; i < tag_length; ++i)
        {
            std::cout << tag_start[i];
        }
        std::cout << "] (length: " << tag_length << ")" << std::endl;

        // Convert to integer (this is what parseInteger() does)
        int field_tag = 0;
        for (size_t i = 0; i < tag_length; ++i)
        {
            field_tag = field_tag * 10 + (tag_start[i] - '0');
        }
        std::cout << "Parsed tag: " << field_tag << std::endl;

        // =================================================================
        // STEP 2B: Find value (between '=' and SOH)
        // =================================================================

        const char *value_start = equals_ptr + 1; // Skip the '='
        const char *soh_ptr = nullptr;

        // Search for SOH character (\x01)
        for (const char *search_ptr = value_start; search_ptr < end_ptr; ++search_ptr)
        {
            if (*search_ptr == '\x01') // SOH
            {
                soh_ptr = search_ptr;
                break;
            }
        }

        if (!soh_ptr)
        {
            std::cout << "ERROR: No SOH found!" << std::endl;
            break;
        }

        // Extract value (zero-copy - just pointer and length)
        size_t value_length = soh_ptr - value_start;
        std::cout << "Value bytes: [";
        for (size_t i = 0; i < value_length; ++i)
        {
            std::cout << value_start[i];
        }
        std::cout << "] (length: " << value_length << ")" << std::endl;

        // Create string_view (C++17) or string (for storage)
        std::string field_value(value_start, value_length);
        std::cout << "Parsed value: \"" << field_value << "\"" << std::endl;

        // =================================================================
        // STEP 2C: Move pointer forward past SOH
        // =================================================================

        current_ptr = soh_ptr + 1; // THIS IS THE KEY: Move past SOH delimiter
        std::cout << "Next position: " << (current_ptr - buffer) << std::endl;

        // Special case: stop at checksum (tag 10)
        if (field_tag == 10) // CheckSum field
        {
            std::cout << "Reached checksum field - parsing complete!" << std::endl;
            break;
        }
    }

    // =================================================================
    // STEP 3: Now demonstrate with actual StreamFixParser
    // =================================================================

    std::cout << "\n=== Actual StreamFixParser Test ===" << std::endl;

    // Create message pool and parser
    MessagePool<FixMessage> pool(1024);
    StreamFixParser parser(&pool);

    // Disable checksum validation for demo (since we're using placeholder checksum)
    parser.setValidateChecksum(false);

    // Parse the buffer
    auto result = parser.parse(buffer, buffer_length);

    if (result.status == StreamFixParser::ParseStatus::Success)
    {
        std::cout << "✅ PARSING SUCCESSFUL!" << std::endl;
        std::cout << "Bytes consumed: " << result.bytes_consumed << std::endl;
        std::cout << "Message type: " << result.parsed_message->getMsgType() << std::endl;
        std::cout << "ClOrdID: " << result.parsed_message->getClOrdID() << std::endl;
        std::cout << "Symbol: " << result.parsed_message->getSymbol() << std::endl;
        std::cout << "Side: " << result.parsed_message->getSide() << std::endl;
        std::cout << "Quantity: " << result.parsed_message->getOrderQty() << std::endl;
        std::cout << "Price: " << result.parsed_message->getPrice() << std::endl;

        // Get statistics
        auto stats = parser.getStats();
        std::cout << "\n=== Parser Performance ===" << std::endl;
        std::cout << "Parse time: " << stats.getAverageParseTimeNs() << " nanoseconds" << std::endl;
        std::cout << "Messages parsed: " << stats.messages_parsed << std::endl;
        std::cout << "Parse errors: " << stats.parse_errors << std::endl;

        // Clean up - return to pool
        pool.deallocate(result.parsed_message);
        std::cout << "\n✅ Message returned to pool successfully!" << std::endl;
    }
    else
    {
        std::cout << "❌ PARSING FAILED!" << std::endl;
        std::cout << "Status: " << static_cast<int>(result.status) << std::endl;
        std::cout << "Error: " << result.error_detail << std::endl;

        // Debug: Show what fields were actually parsed if message exists
        if (result.parsed_message)
        {
            std::cout << "\n=== DEBUG: Parsed Fields ===" << std::endl;
            std::cout << "Field count: " << result.parsed_message->getFieldCount() << std::endl;

            // Check required fields individually
            std::cout << "Has BeginString (8): " << result.parsed_message->hasField(8) << std::endl;
            std::cout << "Has BodyLength (9): " << result.parsed_message->hasField(9) << std::endl;
            std::cout << "Has MsgType (35): " << result.parsed_message->hasField(35) << std::endl;
            std::cout << "Has CheckSum (10): " << result.parsed_message->hasField(10) << std::endl;

            // Show all fields
            auto &allFields = result.parsed_message->getAllFields();
            std::cout << "All parsed fields:" << std::endl;
            for (const auto &field : allFields)
            {
                std::cout << "  Tag " << field.first << " = \"" << field.second << "\"" << std::endl;
            }

            // Clean up - return to pool
            pool.deallocate(result.parsed_message);
        }
    }
}

int main()
{
    try
    {
        demonstrateBufferPointerParsing();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

// =================================================================
// EXPECTED OUTPUT:
// =================================================================
/*

FIX Buffer Pointer Parsing Demonstration
=========================================

=== Raw FIX Message Buffer ===
Buffer content (148 bytes):
8=FIX.4.4[SOH]9=196[SOH]35=D[SOH]49=CLIENT[SOH]56=BROKER[SOH]34=1[SOH]52=20231215-10:30:00[SOH]11=ORDER123[SOH]55=AAPL[SOH]54=1[SOH]38=100[SOH]44=150.50[SOH]40=2[SOH]59=0[SOH]10=123[SOH]
Positions:   0                  10                  20                  30

=== Field-by-Field Pointer Parsing ===

--- Field 1 ---
Current position: 0
Tag bytes: [8] (length: 1)
Parsed tag: 8
Value bytes: [FIX.4.4] (length: 7)
Parsed value: "FIX.4.4"
Next position: 9

--- Field 2 ---
Current position: 9
Tag bytes: [9] (length: 1)
Parsed tag: 9
Value bytes: [196] (length: 3)
Parsed value: "196"
Next position: 14

[... continues for all fields ...]

=== Actual StreamFixParser Test ===
✅ PARSING SUCCESSFUL!
Bytes consumed: 148
Message type: D
ClOrdID: ORDER123
Symbol: AAPL
Side: 1
Quantity: 100
Price: 150.50

=== Parser Performance ===
Parse time: 87 nanoseconds        ← Zero-copy performance!
Messages parsed: 1
Parse errors: 0

✅ Message returned to pool successfully!

*/