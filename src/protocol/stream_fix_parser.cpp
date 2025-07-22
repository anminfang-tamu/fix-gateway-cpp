#include "protocol/stream_fix_parser.h"
#include "protocol/fix_fields.h"
#include "utils/logger.h"
#include "utils/performance_timer.h"
#include <cstring>
#include <algorithm>
#include <sstream>

namespace fix_gateway::protocol
{
    // FIX protocol constants (FIX_SOH is defined in fix_fields.h)
    static constexpr char FIX_EQUALS = '='; // Tag=Value separator
    static constexpr const char *FIX_BEGIN_STRING = "8=FIX.4.4";
    static constexpr const char *FIX_CHECKSUM_TAG = "10=";

    StreamFixParser::StreamFixParser(MessagePool<FixMessage> *message_pool)
        : message_pool_(message_pool), max_message_size_(8192), validate_checksum_(true), strict_validation_(true), partial_buffer_size_(0)
    {
        if (!message_pool_)
        {
            throw std::invalid_argument("MessagePool cannot be null");
        }
    }

    StreamFixParser::~StreamFixParser() = default;

    StreamFixParser::StreamFixParser(StreamFixParser &&other) noexcept
        : message_pool_(other.message_pool_), max_message_size_(other.max_message_size_), validate_checksum_(other.validate_checksum_), strict_validation_(other.strict_validation_), partial_buffer_size_(other.partial_buffer_size_), stats_(other.stats_)
    {
        // Move partial buffer
        std::memcpy(partial_buffer_, other.partial_buffer_, partial_buffer_size_);
        other.partial_buffer_size_ = 0;
        other.message_pool_ = nullptr;
    }

    StreamFixParser &StreamFixParser::operator=(StreamFixParser &&other) noexcept
    {
        if (this != &other)
        {
            message_pool_ = other.message_pool_;
            max_message_size_ = other.max_message_size_;
            validate_checksum_ = other.validate_checksum_;
            strict_validation_ = other.strict_validation_;
            partial_buffer_size_ = other.partial_buffer_size_;
            stats_ = other.stats_;

            std::memcpy(partial_buffer_, other.partial_buffer_, partial_buffer_size_);
            other.partial_buffer_size_ = 0;
            other.message_pool_ = nullptr;
        }
        return *this;
    }

    // =================================================================
    // MAIN PARSE FUNCTION - This is where the magic happens!
    // =================================================================

    StreamFixParser::ParseResult StreamFixParser::parse(const char *buffer, size_t length)
    {
        if (!buffer || length == 0)
        {
            return {ParseStatus::InvalidFormat, 0, nullptr, "Empty buffer"};
        }

        // Start performance timing
        parse_start_time_ = std::chrono::high_resolution_clock::now();

        try
        {
            // Handle partial messages from previous calls
            if (hasPartialMessage())
            {
                return handlePartialMessage(buffer, length);
            }

            // =================================================================
            // STEP 1: Find complete message boundaries
            // =================================================================

            size_t message_start = 0;
            size_t message_end = 0;

            ParseResult boundary_result = findCompleteMessage(buffer, length, message_start, message_end);
            if (boundary_result.status != ParseStatus::Success)
            {
                updateStats(boundary_result.status, 0);
                return boundary_result;
            }

            // =================================================================
            // STEP 2: Parse the complete message (zero-copy)
            // =================================================================

            ParseResult parse_result = parseMessage(buffer, message_start, message_end);

            // Update statistics
            auto parse_end = std::chrono::high_resolution_clock::now();
            auto parse_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  parse_end - parse_start_time_)
                                  .count();
            updateStats(parse_result.status, parse_time);

            return parse_result;
        }
        catch (const std::exception &e)
        {
            updateStats(ParseStatus::InvalidFormat, 0);
            return {ParseStatus::InvalidFormat, 0, nullptr, "Parse exception: " + std::string(e.what())};
        }
    }

    // =================================================================
    // CORE ZERO-COPY PARSING LOGIC
    // =================================================================

    StreamFixParser::ParseResult StreamFixParser::parseMessage(const char *buffer, size_t start_pos, size_t end_pos)
    {
        // Allocate FixMessage from pool (zero allocation - pre-allocated)
        FixMessage *message = message_pool_->allocate();
        if (!message)
        {
            return {ParseStatus::AllocationFailed, 0, nullptr, "MessagePool allocation failed"};
        }

        // =================================================================
        // ZERO-COPY FIELD EXTRACTION: Parse fields using buffer pointers
        // =================================================================

        const char *current_ptr = buffer + start_pos;
        const char *end_ptr = buffer + end_pos;

        while (current_ptr < end_ptr)
        {
            // =================================================================
            // STEP 1: Find tag (number before '=')
            // =================================================================

            const char *tag_start = current_ptr;
            const char *equals_ptr = std::find(current_ptr, end_ptr, FIX_EQUALS);

            if (equals_ptr == end_ptr)
            {
                // No '=' found - malformed field
                message_pool_->deallocate(message);
                return {ParseStatus::InvalidFormat, static_cast<size_t>(current_ptr - buffer), nullptr,
                        "Missing '=' in field at position " + std::to_string(current_ptr - buffer)};
            }

            // Parse tag as integer (zero-copy)
            int field_tag = 0;
            if (!parseInteger(tag_start, static_cast<size_t>(equals_ptr - tag_start), field_tag))
            {
                message_pool_->deallocate(message);
                return {ParseStatus::InvalidFormat, static_cast<size_t>(current_ptr - buffer), nullptr,
                        "Invalid tag number at position " + std::to_string(tag_start - buffer)};
            }

            // =================================================================
            // STEP 2: Find value (between '=' and SOH)
            // =================================================================

            const char *value_start = equals_ptr + 1; // Skip the '='
            const char *soh_ptr = std::find(value_start, end_ptr, FIX_SOH);

            if (soh_ptr == end_ptr)
            {
                // No SOH found - this should not happen for complete messages
                message_pool_->deallocate(message);
                return {ParseStatus::InvalidFormat, static_cast<size_t>(value_start - buffer), nullptr,
                        "Missing SOH after field " + std::to_string(field_tag)};
            }

            // =================================================================
            // STEP 3: Extract value as string (ZERO-COPY using string_view)
            // =================================================================

            size_t value_length = soh_ptr - value_start;
            std::string field_value(value_start, value_length); // Only copy when storing in FixMessage

            // Store field in message
            message->setField(field_tag, field_value);

            // =================================================================
            // STEP 4: Move pointer forward past SOH delimiter
            // =================================================================

            current_ptr = soh_ptr + 1; // Move past the SOH character

            // Special handling for checksum field (last field)
            if (field_tag == FixFields::CheckSum)
            {
                break; // Checksum is always the last field
            }
        }

        // =================================================================
        // STEP 5: Validate parsed message
        // =================================================================

        if (strict_validation_)
        {
            if (!validateParsedMessage(message))
            {
                message_pool_->deallocate(message);
                return {ParseStatus::InvalidFormat, 0, nullptr, "Message validation failed"};
            }
        }

        // =================================================================
        // STEP 6: Validate checksum if enabled
        // =================================================================

        if (validate_checksum_)
        {
            if (!validateMessageChecksum(buffer + start_pos, end_pos - start_pos))
            {
                message_pool_->deallocate(message);
                return {ParseStatus::ChecksumError, 0, nullptr, "Checksum validation failed"};
            }
        }

        return {ParseStatus::Success, end_pos - start_pos, message, ""};
    }

    // =================================================================
    // HELPER FUNCTIONS: Buffer pointer manipulation
    // =================================================================

    StreamFixParser::ParseResult StreamFixParser::findCompleteMessage(
        const char *buffer, size_t length, size_t &message_start, size_t &message_end)

    // Index :  0  1  2  3  4  5  6  7  8   9  10 11 12  13 14  15 16  17 18  19 20 21 22 23 24 25 26
    // Char  :  8  =  F  I  X  .  4  .  4  SOH 9  =  1   2  SOH 3  5   =  0  SOH 1  0  =  1  2  3  SOH
    // 8=FIX.4.4\x019=12\x0135=D\x0110=123\x01
    {
        // =================================================================
        // STEP 1: Find BeginString (8=FIX.4.4)
        // =================================================================

        const char *begin_ptr = std::search(buffer, buffer + length,
                                            FIX_BEGIN_STRING, FIX_BEGIN_STRING + strlen(FIX_BEGIN_STRING));

        if (begin_ptr == buffer + length)
        {
            // No BeginString found - store as partial message
            storePartialMessage(buffer, length);
            return {ParseStatus::NeedMoreData, 0, nullptr, "BeginString not found"};
        }

        message_start = static_cast<size_t>(begin_ptr - buffer);

        // =================================================================
        // STEP 2: Find BodyLength field (9=XXX)
        // =================================================================

        // begin_ptr is standing at 8=FIX.4.4

        const char *current_ptr = begin_ptr;

        // Skip past BeginString field and find the first SOH
        current_ptr = std::find(current_ptr, buffer + length, FIX_SOH);
        if (current_ptr == buffer + length)
        {
            storePartialMessage(buffer + message_start, length - message_start);
            return {ParseStatus::NeedMoreData, 0, nullptr, "Incomplete BeginString field"};
        }
        current_ptr++; // Skip SOH

        // now current_ptr should be standing at 9, if not, then the message is malformed
        // look for BodyLength field (9=)
        // check if the next 2 characters is still within the buffer and if they are '9' and '='
        if (current_ptr + 2 >= buffer + length || current_ptr[0] != '9' || current_ptr[1] != '=')
        {
            return {ParseStatus::InvalidFormat, static_cast<size_t>(current_ptr - buffer), nullptr, "BodyLength field not found after BeginString"};
        }

        // Parse body length value
        current_ptr += 2; // Skip "9="
        const char *body_length_start = current_ptr;
        const char *body_length_end = std::find(current_ptr, buffer + length, FIX_SOH); // find the next SOH

        if (body_length_end == buffer + length)
        {
            storePartialMessage(buffer + message_start, length - message_start);
            return {ParseStatus::NeedMoreData, 0, nullptr, "Incomplete BodyLength field"};
        }

        // Convert body length to integer
        // body_length_start is standing at 9, body_length_end is standing at the next SOH
        int body_length = 0;
        if (!parseInteger(body_length_start, static_cast<size_t>(body_length_end - body_length_start), body_length))
        {
            return {ParseStatus::InvalidFormat, static_cast<size_t>(body_length_start - buffer), nullptr, "Invalid BodyLength value"};
        }

        // =================================================================
        // STEP 3: Calculate message end position
        // =================================================================

        // Message structure: BeginString + SOH + BodyLength + SOH + [body_length bytes] + CheckSum + SOH
        // message_start is standing at 8=FIX.4.4
        size_t header_size = (body_length_end + 1) - (begin_ptr); // +1 for SOH after body length
        message_end = message_start + header_size + body_length;

        // Check if we have the complete message
        if (message_end > length)
        {
            storePartialMessage(buffer + message_start, length - message_start);
            return {ParseStatus::NeedMoreData, 0, nullptr,
                    "Need " + std::to_string(message_end - length) + " more bytes"};
        }

        return {ParseStatus::Success, 0, nullptr, ""};
    }

    bool StreamFixParser::parseInteger(const char *buffer, size_t length, int &result)
    {
        if (!buffer || length == 0)
            return false;

        result = 0;
        for (size_t i = 0; i < length; ++i)
        {
            if (buffer[i] < '0' || buffer[i] > '9')
                return false;

            result = result * 10 + (buffer[i] - '0');
        }
        return true;
    }

    bool StreamFixParser::validateParsedMessage(FixMessage *message)
    {
        if (!message)
            return false;

        // Check required fields
        return message->hasField(FixFields::BeginString) &&
               message->hasField(FixFields::BodyLength) &&
               message->hasField(FixFields::MsgType) &&
               message->hasField(FixFields::CheckSum);
    }

    bool StreamFixParser::validateMessageChecksum(const char *buffer, size_t length)
    {
        // Find checksum field (10=XXX) - should be at the end
        const char *checksum_pos = buffer + length - 7; // "10=XXX\x01" = 7 characters

        if (checksum_pos < buffer ||
            checksum_pos[0] != '1' || checksum_pos[1] != '0' || checksum_pos[2] != '=')
        {
            return false;
        }

        // Calculate actual checksum (sum of all bytes before checksum field)
        uint8_t calculated_checksum = 0;
        for (const char *ptr = buffer; ptr < checksum_pos; ++ptr)
        {
            calculated_checksum += static_cast<uint8_t>(*ptr);
        }
        calculated_checksum %= 256;

        // Parse expected checksum
        int expected_checksum = 0;
        if (!parseInteger(checksum_pos + 3, 3, expected_checksum))
        {
            return false;
        }

        return calculated_checksum == static_cast<uint8_t>(expected_checksum);
    }

    // =================================================================
    // PARTIAL MESSAGE HANDLING (TCP fragmentation)
    // =================================================================

    StreamFixParser::ParseResult StreamFixParser::handlePartialMessage(const char *new_buffer, size_t new_length)
    {
        // Combine partial buffer with new data
        size_t total_length = partial_buffer_size_ + new_length;

        if (total_length > PARTIAL_BUFFER_SIZE)
        {
            reset(); // Clear partial buffer
            return {ParseStatus::MessageTooLarge, 0, nullptr, "Combined message too large"};
        }

        // Append new data to partial buffer
        std::memcpy(partial_buffer_ + partial_buffer_size_, new_buffer, new_length);

        // Try to parse the combined buffer
        ParseResult result = parse(partial_buffer_, total_length);

        if (result.status == ParseStatus::Success)
        {
            // Successfully parsed - clear partial buffer
            partial_buffer_size_ = 0;
        }
        else if (result.status == ParseStatus::NeedMoreData)
        {
            // Still incomplete - update partial buffer size
            partial_buffer_size_ = total_length;
        }
        else
        {
            // Parse error - clear partial buffer
            partial_buffer_size_ = 0;
        }

        return result;
    }

    void StreamFixParser::storePartialMessage(const char *buffer, size_t length)
    {
        if (length > 0 && length <= PARTIAL_BUFFER_SIZE)
        {
            std::memcpy(partial_buffer_, buffer, length);
            partial_buffer_size_ = length;
        }
    }

    void StreamFixParser::reset()
    {
        partial_buffer_size_ = 0;
        std::memset(partial_buffer_, 0, sizeof(partial_buffer_));
    }

    // =================================================================
    // STATISTICS TRACKING
    // =================================================================

    void StreamFixParser::updateStats(ParseStatus status, uint64_t parse_time_ns)
    {
        if (status == ParseStatus::Success)
        {
            stats_.messages_parsed++;
            stats_.total_parse_time_ns += parse_time_ns;
            stats_.max_parse_time_ns = std::max(stats_.max_parse_time_ns, parse_time_ns);
            stats_.min_parse_time_ns = std::min(stats_.min_parse_time_ns, parse_time_ns);
        }
        else
        {
            stats_.parse_errors++;
            if (status == ParseStatus::ChecksumError)
                stats_.checksum_errors++;
            if (status == ParseStatus::AllocationFailed)
                stats_.allocation_failures++;
        }
    }

} // namespace fix_gateway::protocol