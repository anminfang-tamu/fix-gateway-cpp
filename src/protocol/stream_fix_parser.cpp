#include "protocol/stream_fix_parser.h"
#include "protocol/fix_fields.h"
#include "utils/logger.h"
#include "utils/performance_timer.h"
#include <cstring>
#include <algorithm>
#include <sstream>
#include <chrono> // Added for steady_clock used in circuit breaker

namespace fix_gateway::protocol
{
    // FIX protocol constants (FIX_SOH is defined in fix_fields.h)
    static constexpr char FIX_EQUALS = '='; // Tag=Value separator
    static constexpr const char *FIX_BEGIN_STRING = "8=FIX.4.4";
    static constexpr const char *FIX_CHECKSUM_TAG = "10=";

    // Utility function to convert ParseState enum to string for debugging
    static std::string parseStateToString(StreamFixParser::ParseState state)
    {
        switch (state)
        {
        case StreamFixParser::ParseState::IDLE:
            return "IDLE";
        case StreamFixParser::ParseState::PARSING_BEGIN_STRING:
            return "PARSING_BEGIN_STRING";
        case StreamFixParser::ParseState::PARSING_BODY_LENGTH:
            return "PARSING_BODY_LENGTH";
        case StreamFixParser::ParseState::PARSING_TAG:
            return "PARSING_TAG";
        case StreamFixParser::ParseState::EXPECTING_EQUALS:
            return "EXPECTING_EQUALS";
        case StreamFixParser::ParseState::PARSING_VALUE:
            return "PARSING_VALUE";
        case StreamFixParser::ParseState::EXPECTING_SOH:
            return "EXPECTING_SOH";
        case StreamFixParser::ParseState::PARSING_CHECKSUM:
            return "PARSING_CHECKSUM";
        case StreamFixParser::ParseState::MESSAGE_COMPLETE:
            return "MESSAGE_COMPLETE";
        case StreamFixParser::ParseState::ERROR_RECOVERY:
            return "ERROR_RECOVERY";
        case StreamFixParser::ParseState::CORRUPTED_SKIP:
            return "CORRUPTED_SKIP";
        default:
            return "UNKNOWN_STATE";
        }
    }

    StreamFixParser::StreamFixParser(MessagePool<FixMessage> *message_pool)
        : message_pool_(message_pool),
          max_message_size_(8192),
          validate_checksum_(true),
          strict_validation_(true),
          max_consecutive_errors_(10),                              // Circuit breaker threshold
          error_recovery_enabled_(true),                            // Enable error recovery
          error_recovery_timeout_(std::chrono::milliseconds(1000)), // 1 second timeout
          partial_buffer_size_(0),
          circuit_breaker_active_(false) // Circuit breaker inactive initially
    {
        if (!message_pool_)
        {
            throw std::invalid_argument("MessagePool cannot be null");
        }

        // Initialize circuit breaker timestamp
        circuit_breaker_last_reset_ = std::chrono::steady_clock::now();

        // Initialize parse context to IDLE state
        parse_context_.reset();
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
    // ENHANCED MAIN PARSE FUNCTION - State Machine Implementation
    // 2 stages:
    // 1. Framing: Find complete messages
    // 2. Message decode: Parse complete messages
    // =================================================================

    StreamFixParser::ParseResult StreamFixParser::parse(const char *buf, size_t len)
    {
        if (!buf || len == 0)
        {
            return {ParseStatus::InvalidFormat, 0, nullptr, "Empty buffer", ParseState::IDLE, 0};
        }

        // Start performance timing
        parse_start_time_ = std::chrono::high_resolution_clock::now();

        try
        {
            // Check circuit breaker status
            if (isCircuitBreakerActive())
            {
                return {ParseStatus::CorruptedData, 0, nullptr, "Circuit breaker active - too many consecutive errors",
                        ParseState::ERROR_RECOVERY, 0};
            }

            // =================================================================
            // STAGE 1: FRAMING - Handle partial buffer and find complete messages
            // =================================================================

            // ❶ Prepend any leftover partial bytes
            if (partial_buffer_size_ != 0)
            {
                if (partial_buffer_size_ + len > PARTIAL_BUFFER_SIZE)
                {
                    return {ParseStatus::MessageTooLarge, 0, nullptr,
                            "Partial buffer overflow", ParseState::ERROR_RECOVERY, 0};
                }

                std::memcpy(partial_buffer_ + partial_buffer_size_, buf, len);
                buf = partial_buffer_;
                len += partial_buffer_size_;
                partial_buffer_size_ = 0;
            }

            size_t cursor = 0;

            while (cursor < len)
            {
                size_t msgStart, msgEnd;
                ParseResult frameRes = findCompleteMessage(buf + cursor,      // src
                                                           len - cursor,      // avail
                                                           msgStart, msgEnd); // OUT

                if (frameRes.status == ParseStatus::NeedMoreData)
                {
                    // Copy leftovers to partial_buffer_ for next call
                    //
                    // EXAMPLE: Why leftover = len - cursor
                    //
                    // Scenario 1: First message is incomplete
                    // Buffer layout:
                    // [0────────────49] len=50
                    //  ^cursor=0
                    //  └─ Incomplete FIX message (needs more data)
                    //
                    // leftover = len - cursor = 50 - 0 = 50 bytes
                    // → Save entire buffer for next parse() call
                    //
                    // Scenario 2: Second message is incomplete
                    // Buffer layout:
                    // [0─────29][30──────49] len=50
                    //  ^complete  ^cursor=30
                    //  message    └─ Incomplete message (needs more data)
                    //
                    // leftover = len - cursor = 50 - 30 = 20 bytes
                    // → Save only the incomplete portion starting at cursor
                    //
                    // The key insight: cursor tracks our current parsing position,
                    // so (len - cursor) gives us exactly the unprocessed bytes
                    // that need to be preserved for the next parse() call.

                    size_t leftover = len - cursor;
                    if (leftover > 0)
                    {
                        std::memcpy(partial_buffer_, buf + cursor, leftover);
                        partial_buffer_size_ = leftover;
                    }
                    return frameRes; // Not an error – we just wait for more data
                }

                if (frameRes.status != ParseStatus::Success)
                {
                    return frameRes; // Malformed header etc. – let caller decide
                }

                // =================================================================
                // STAGE 2: MESSAGE DECODE - We now have ONE complete FIX message
                // =================================================================

                const char *msgPtr = buf + cursor + msgStart; // Usually msgStart == 0
                size_t msgLen = msgEnd - msgStart;

                ParseResult decodeRes = parseCompleteMessage(msgPtr, msgLen);
                decodeRes.bytes_consumed = cursor + msgEnd; // Absolute position in original buffer

                // Update statistics
                auto parse_end = std::chrono::high_resolution_clock::now();
                auto parse_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      parse_end - parse_start_time_)
                                      .count();

                if (decodeRes.status == ParseStatus::Success)
                {
                    resetErrorRecovery();
                    recordPartialMessageHandled();
                    updateStats(decodeRes.status, parse_time);
                }
                else if (decodeRes.status != ParseStatus::NeedMoreData)
                {
                    updateErrorStats(decodeRes.status, decodeRes.final_state);
                    updateStats(decodeRes.status, parse_time);

                    // Attempt error recovery if enabled
                    if (error_recovery_enabled_ && canRecoverFromError(decodeRes.status, decodeRes.final_state))
                    {
                        ParseResult recovery_result = attemptErrorRecovery(msgPtr, msgLen, parse_context_, decodeRes.error_detail);
                        if (recovery_result.status == ParseStatus::RecoverySuccess)
                        {
                            decodeRes = recovery_result;
                            decodeRes.bytes_consumed = cursor + msgEnd;
                            recordErrorRecovery(true);
                        }
                        else
                        {
                            recordErrorRecovery(false);
                        }
                    }
                }

                // For single-message API, return immediately
                return decodeRes;

                // Note: For multi-message support, you would:
                // cursor += msgEnd;
                // if (decodeRes.status != Success) return decodeRes;
                // otherwise continue loop to peel next message
            }

            // We only get here if len == 0 after processing
            return {ParseStatus::NeedMoreData, 0, nullptr, "No data to process", ParseState::IDLE, 0};
        }
        catch (const std::exception &e)
        {
            // Handle unexpected exceptions
            updateStats(ParseStatus::InvalidFormat, 0);
            parse_context_.error_count_in_session++;

            return {ParseStatus::InvalidFormat, 0, nullptr,
                    "Parse exception: " + std::string(e.what()),
                    ParseState::ERROR_RECOVERY, 0};
        }
    }

    // =================================================================
    // STATE MACHINE CORE PROCESSOR
    // =================================================================

    StreamFixParser::ParseResult StreamFixParser::processStateMachine(const char *buffer, size_t length, ParseContext &context)
    {
        ParseResult result;
        size_t total_consumed = 0;

        // Process buffer until complete message or need more data
        while (total_consumed <= length)
        {
            const char *current_buffer = buffer + total_consumed;
            size_t remaining_length = length - total_consumed;

            // Update buffer position in context
            context.buffer_position = total_consumed;

            // Store the previous state to detect transitions
            ParseState previous_state = context.current_state;

            // For MESSAGE_COMPLETE state, we don't need buffer data
            if (context.current_state == ParseState::MESSAGE_COMPLETE && remaining_length == 0)
            {
                // Call handleMessageComplete with empty buffer
                result = handleMessageComplete(nullptr, 0, context);
            }
            else
            {
                // Dispatch to appropriate state handler
                switch (context.current_state)
                {
                case ParseState::IDLE:
                    result = handleIdleState(current_buffer, remaining_length, context);
                    break;

                case ParseState::PARSING_BEGIN_STRING:
                    result = handleParsingBeginString(current_buffer, remaining_length, context);
                    break;

                case ParseState::PARSING_BODY_LENGTH:
                    result = handleParsingBodyLength(current_buffer, remaining_length, context);
                    break;

                case ParseState::PARSING_TAG:
                    result = handleParsingTag(current_buffer, remaining_length, context);
                    break;

                case ParseState::EXPECTING_EQUALS:
                    result = handleExpectingEquals(current_buffer, remaining_length, context);
                    break;

                case ParseState::PARSING_VALUE:
                    result = handleParsingValue(current_buffer, remaining_length, context);
                    break;

                case ParseState::EXPECTING_SOH:
                    result = handleExpectingSOH(current_buffer, remaining_length, context);
                    break;

                case ParseState::PARSING_CHECKSUM:
                    result = handleParsingChecksum(current_buffer, remaining_length, context);
                    break;

                case ParseState::MESSAGE_COMPLETE:
                    result = handleMessageComplete(current_buffer, remaining_length, context);
                    break;

                case ParseState::ERROR_RECOVERY:
                    result = handleErrorRecovery(current_buffer, remaining_length, context);
                    break;

                case ParseState::CORRUPTED_SKIP:
                    result = handleCorruptedSkip(current_buffer, remaining_length, context);
                    break;

                default:
                    return {ParseStatus::StateTransitionError, total_consumed, nullptr,
                            "Invalid parser state: " + std::to_string(static_cast<int>(context.current_state)),
                            context.current_state, total_consumed};
                }
            }

            // Update total bytes consumed
            size_t bytes_consumed_this_iteration = result.bytes_consumed;
            total_consumed += result.bytes_consumed;
            result.bytes_consumed = total_consumed; // Update to total consumed

            // Check if we made a state transition
            bool state_changed = (context.current_state != previous_state);

            // Handle different result statuses
            if (result.status == ParseStatus::Success)
            {
                return result;
            }
            else if (result.status == ParseStatus::NeedMoreData)
            {
                // Only return NeedMoreData if we've consumed all available bytes
                // or if no bytes were consumed AND no state transition occurred (truly need more data)
                if (total_consumed >= length || (bytes_consumed_this_iteration == 0 && !state_changed))
                {
                    return result;
                }
                // Otherwise continue processing - we made progress (either consumed bytes or changed state)
            }
            else if (result.status == ParseStatus::RecoverySuccess)
            {
                return result;
            }
            else if (result.status == ParseStatus::InvalidFormat ||
                     result.status == ParseStatus::ChecksumError ||
                     result.status == ParseStatus::FieldParseError)
            {
                // Error occurred - return or attempt recovery
                return result;
            }

            // Continue processing if state changed but no definitive result
            recordStateTransition();
        }

        // Reached end of buffer without completing message
        if (context.current_state != ParseState::IDLE)
        {
            // Store partial message for next call
            storePartialMessage(buffer, length);
            stats_.partial_messages_handled++;

            return {ParseStatus::NeedMoreData, length, nullptr,
                    "Partial message stored, need more data",
                    context.current_state, 0};
        }

        return {ParseStatus::Success, length, nullptr, "Buffer processed completely",
                context.current_state, 0};
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

    StreamFixParser::ParseResult StreamFixParser::parseCompleteMessage(const char *buffer, size_t length)
    {
        if (!buffer || length == 0)
        {
            return {ParseStatus::InvalidFormat, 0, nullptr, "Empty complete message buffer", ParseState::IDLE, 0};
        }

        // =================================================================
        // STAGE 2: INTELLIGENT MESSAGE PARSING
        // =================================================================

        // Start with intelligent parsing that can dispatch to optimized templates
        ParseResult result = parseIntelligent(buffer, length);

        // If intelligent parsing succeeded, we're done
        if (result.status == ParseStatus::Success)
        {
            return result;
        }

        // =================================================================
        // FALLBACK: LEGACY PARSING for complex/unusual messages
        // =================================================================

        // For messages that don't fit the optimized templates or have parsing issues,
        // fall back to the legacy parseMessage method with message boundaries
        size_t message_start = 0;
        size_t message_end = length;

        // The buffer should already contain a complete message, so we can parse directly
        ParseResult fallback_result = parseMessage(buffer, message_start, message_end);

        // Ensure bytes_consumed matches the complete message length
        if (fallback_result.status == ParseStatus::Success)
        {
            fallback_result.bytes_consumed = length;
            fallback_result.final_state = ParseState::IDLE; // Ready for next message
        }

        return fallback_result;
    }

    // =================================================================
    // HELPER FUNCTIONS: Buffer pointer manipulation
    // =================================================================

    StreamFixParser::ParseResult StreamFixParser::findCompleteMessage(
        const char *buffer, size_t length, size_t &message_start, size_t &message_end)
    {
        // =================================================================
        // STAGE 1: FRAMING - Find complete FIX message boundaries
        // =================================================================
        //
        // Example FIX message structure:
        // Index:  0  1  2  3  4  5  6  7  8   9  10 11 12  13 14  15 16  17 18  19 20 21 22 23 24 25 26
        // Chars:  8  =  F  I  X  .  4  .  4  SOH 9  =  1   2  SOH 3  5   =  D  SOH 1  0  =  1  2  3  SOH
        //         ^--- BeginString -----^     ^- BodyLength-^     ^---- Body ----^     ^-- Checksum --^

        // Initialize output parameters
        message_start = 0;
        message_end = 0;

        if (!buffer || length < 10) // Minimum: "8=FIX.4.4\x01" (10 bytes)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Buffer too small for FIX message", ParseState::IDLE, 0};
        }

        // =================================================================
        // STEP 1: Find BeginString (8=FIX.4.4)
        // =================================================================

        const char *begin_ptr = std::search(buffer, buffer + length,
                                            FIX_BEGIN_STRING, FIX_BEGIN_STRING + strlen(FIX_BEGIN_STRING));

        if (begin_ptr == buffer + length)
        {
            // No BeginString found - not necessarily an error, might need more data
            return {ParseStatus::NeedMoreData, 0, nullptr, "BeginString not found", ParseState::IDLE, 0};
        }

        message_start = static_cast<size_t>(begin_ptr - buffer);

        // =================================================================
        // STEP 2: Validate and parse BodyLength field (9=XXX)
        // =================================================================

        const char *current_ptr = begin_ptr;

        // Skip past BeginString field to find the SOH delimiter
        current_ptr = std::find(current_ptr, buffer + length, FIX_SOH);
        if (current_ptr == buffer + length)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Incomplete BeginString field", ParseState::PARSING_BEGIN_STRING, 0};
        }
        current_ptr++; // Skip SOH, now should point to '9'

        // Verify we have enough bytes left for BodyLength field "9=X\x01" (minimum 4 bytes)
        if (current_ptr + 4 > buffer + length)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Not enough data for BodyLength field", ParseState::PARSING_BODY_LENGTH, 0};
        }

        // Validate BodyLength field format "9="
        if (current_ptr[0] != '9' || current_ptr[1] != '=')
        {
            return {ParseStatus::InvalidFormat, static_cast<size_t>(current_ptr - buffer), nullptr,
                    "BodyLength field not found after BeginString", ParseState::ERROR_RECOVERY, 0};
        }

        // Parse body length value
        current_ptr += 2; // Skip "9="
        const char *body_length_start = current_ptr;
        const char *body_length_end = std::find(current_ptr, buffer + length, FIX_SOH);

        if (body_length_end == buffer + length)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Incomplete BodyLength value", ParseState::PARSING_BODY_LENGTH, 0};
        }

        // Convert body length to integer
        int body_length = 0;
        if (!parseInteger(body_length_start, static_cast<size_t>(body_length_end - body_length_start), body_length))
        {
            return {ParseStatus::InvalidFormat, static_cast<size_t>(body_length_start - buffer), nullptr,
                    "Invalid BodyLength value", ParseState::ERROR_RECOVERY, 0};
        }

        // Validate body length is reasonable
        if (body_length <= 0 || body_length > static_cast<int>(max_message_size_))
        {
            return {ParseStatus::InvalidFormat, static_cast<size_t>(body_length_start - buffer), nullptr,
                    "BodyLength value out of range: " + std::to_string(body_length), ParseState::ERROR_RECOVERY, 0};
        }

        parse_context_.expected_body_length = body_length;

        // =================================================================
        // STEP 3: Calculate complete message boundaries
        // =================================================================

        // Message structure: BeginString + SOH + BodyLength + SOH + [body_length bytes]
        // The body_length includes everything from after BodyLength field to end of message (including checksum)
        size_t header_size = (body_length_end + 1) - begin_ptr; // +1 for SOH after body length
        message_end = message_start + header_size + static_cast<size_t>(body_length);

        // Verify we have the complete message
        if (message_end > length)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr,
                    "Need " + std::to_string(message_end - length) + " more bytes for complete message",
                    ParseState::PARSING_TAG, 0};
        }

        // =================================================================
        // STEP 4: Basic sanity check - verify message ends with checksum
        // =================================================================

        if (message_end >= 7) // Ensure we have room for "10=XXX\x01"
        {
            const char *checksum_start = buffer + message_end - 7;

            // Optional: Quick checksum field validation (10=XXX)
            if (checksum_start >= buffer &&
                checksum_start[0] == '1' && checksum_start[1] == '0' && checksum_start[2] == '=' &&
                buffer[message_end - 1] == FIX_SOH)
            {
                // Message looks well-formed
                return {ParseStatus::Success, 0, nullptr, "Complete message found", ParseState::IDLE, 0};
            }
        }

        // Message boundary calculation succeeded, but structure might be malformed
        // Let the parsing stage handle detailed validation
        return {ParseStatus::Success, 0, nullptr, "Message boundaries determined", ParseState::IDLE, 0};
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
        partial_buffer_size_ += new_length;

        // reset the parse context
        parse_context_.reset();

        // Dispatch directly into the core state machine to avoid recursion
        ParseResult result = processStateMachine(partial_buffer_, partial_buffer_size_, parse_context_);

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
        parse_context_.reset();
        resetErrorRecovery();
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

    // =================================================================
    // STATE HANDLERS IMPLEMENTATION
    // =================================================================

    StreamFixParser::ParseResult StreamFixParser::handleIdleState(const char *buffer, size_t length, ParseContext &context)
    {
        // Start looking for BeginString (8=FIX.4.4)
        if (length < 9) // Minimum size for "8=FIX.4.4"
        {
            partial_buffer_size_ += length;
            return {ParseStatus::NeedMoreData, 0, nullptr, "Not enough data for BeginString",
                    ParseState::IDLE, 0};
        }

        // Look for FIX begin string
        const char *fix_start = std::search(buffer, buffer + length,
                                            FIX_BEGIN_STRING, FIX_BEGIN_STRING + strlen(FIX_BEGIN_STRING));

        if (fix_start == buffer + length)
        {
            // No BeginString found - might be corrupted data
            if (length > 512) // If we've scanned a lot without finding BeginString
            {
                return attemptErrorRecovery(buffer, length, context, "BeginString not found in large buffer");
            }
            return {ParseStatus::NeedMoreData, 0, nullptr, "BeginString not found",
                    ParseState::IDLE, 0};
        }

        // Found BeginString - calculate position
        size_t begin_string_pos = fix_start - buffer;
        context.message_start_pos = begin_string_pos;

        // Skip any corrupted data before BeginString
        if (begin_string_pos > 0)
        {
            return {ParseStatus::NeedMoreData, begin_string_pos, nullptr,
                    "Skipped " + std::to_string(begin_string_pos) + " bytes to find BeginString",
                    ParseState::IDLE, 0};
        }

        // BeginString found at start of buffer - transition to PARSING_BEGIN_STRING state
        // to properly validate and consume it
        if (!transitionToState(ParseState::PARSING_BEGIN_STRING, context))
        {
            return {ParseStatus::StateTransitionError, 0, nullptr,
                    "Failed to transition to PARSING_BEGIN_STRING", ParseState::ERROR_RECOVERY, 0};
        }

        return {ParseStatus::NeedMoreData, begin_string_pos, nullptr, "BeginString located, transitioning to validation",
                ParseState::PARSING_BEGIN_STRING, 0};
    }

    StreamFixParser::ParseResult StreamFixParser::handleParsingBeginString(const char *buffer, size_t length, ParseContext &context)
    {
        // In PARSING_BEGIN_STRING state, we need to validate and consume the BeginString
        // The buffer should be positioned at the start of "8=FIX.4.4"

        size_t consumed = 0;
        if (!validateBeginString(buffer, length, consumed))
        {
            return {ParseStatus::InvalidFormat, 0, nullptr, "Invalid BeginString format",
                    ParseState::ERROR_RECOVERY, 0};
        }

        if (consumed == 0)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Need more data to complete BeginString",
                    ParseState::PARSING_BEGIN_STRING, 0};
        }

        // BeginString validated and consumed - transition to parsing body length
        if (!transitionToState(ParseState::PARSING_BODY_LENGTH, context))
        {
            return {ParseStatus::StateTransitionError, consumed, nullptr,
                    "Failed to transition to PARSING_BODY_LENGTH", ParseState::ERROR_RECOVERY, consumed};
        }

        return {ParseStatus::NeedMoreData, consumed, nullptr, "BeginString validated, transitioning to BodyLength",
                ParseState::PARSING_BODY_LENGTH, 0};
    }

    StreamFixParser::ParseResult StreamFixParser::handleParsingBodyLength(const char *buffer, size_t length, ParseContext &context)
    {
        size_t consumed = 0;
        if (!validateBodyLength(buffer, length, context, consumed))
        {
            return {ParseStatus::InvalidFormat, consumed, nullptr, "Invalid BodyLength format",
                    ParseState::ERROR_RECOVERY, consumed};
        }

        if (consumed == 0)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Need more data for BodyLength",
                    ParseState::PARSING_BODY_LENGTH, 0};
        }

        // Transition to parsing regular fields - we successfully parsed the body length
        if (!transitionToState(ParseState::PARSING_TAG, context))
        {
            return {ParseStatus::StateTransitionError, consumed, nullptr,
                    "Failed to transition to PARSING_TAG", ParseState::ERROR_RECOVERY, consumed};
        }

        return {ParseStatus::NeedMoreData, consumed, nullptr, "BodyLength parsed, transitioning to field parsing",
                ParseState::PARSING_TAG, 0};
    }

    StreamFixParser::ParseResult StreamFixParser::handleParsingTag(const char *buffer, size_t length, ParseContext &context)
    {
        // In PARSING_TAG state, we need to parse the field tag number (digits before '=')

        // Find the '=' character that separates tag from value
        const char *equals_pos = std::find(buffer, buffer + length, '=');

        if (equals_pos == buffer + length)
        {
            // No '=' found - might need more data or could be malformed
            if (length > 10) // Field tags shouldn't be longer than ~5 digits typically
            {
                return {ParseStatus::InvalidFormat, 0, nullptr, "Field tag too long or missing '='",
                        ParseState::ERROR_RECOVERY, 0};
            }

            return {ParseStatus::NeedMoreData, 0, nullptr, "Need more data to find '=' after tag",
                    ParseState::PARSING_TAG, 0};
        }

        // Calculate tag length
        size_t tag_length = equals_pos - buffer;

        if (tag_length == 0)
        {
            return {ParseStatus::InvalidFormat, 0, nullptr, "Empty field tag",
                    ParseState::ERROR_RECOVERY, 0};
        }

        // Parse the tag number
        int field_tag = 0;
        if (!parseInteger(buffer, tag_length, field_tag))
        {
            return {ParseStatus::FieldParseError, tag_length, nullptr,
                    "Invalid field tag number: " + std::string(buffer, tag_length),
                    ParseState::ERROR_RECOVERY, 0};
        }

        // Validate the field tag
        if (!isValidFieldTag(field_tag))
        {
            return {ParseStatus::FieldParseError, tag_length, nullptr,
                    "Invalid field tag value: " + std::to_string(field_tag),
                    ParseState::ERROR_RECOVERY, 0};
        }

        // Store the parsed tag in context
        context.current_field_tag = field_tag;

        // Calculate bytes consumed (just the tag, not the '=')
        size_t consumed = tag_length;

        // Transition to expecting equals state (we found '=' but haven't consumed it yet)
        if (!transitionToState(ParseState::EXPECTING_EQUALS, context))
        {
            return {ParseStatus::StateTransitionError, consumed, nullptr,
                    "Failed to transition to EXPECTING_EQUALS", ParseState::ERROR_RECOVERY, consumed};
        }

        return {ParseStatus::NeedMoreData, consumed, nullptr,
                "Field tag " + std::to_string(field_tag) + " parsed successfully",
                ParseState::EXPECTING_EQUALS, 0};
    }

    StreamFixParser::ParseResult StreamFixParser::handleExpectingEquals(const char *buffer, size_t length, ParseContext &context)
    {
        // In EXPECTING_EQUALS state, we should be positioned at the '=' character
        // We just need to validate it's there and consume it

        if (length == 0)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Need more data for '=' character",
                    ParseState::EXPECTING_EQUALS, 0};
        }

        // Check that the first character is '='
        if (buffer[0] != '=')
        {
            return {ParseStatus::InvalidFormat, 0, nullptr,
                    "Expected '=' after field tag " + std::to_string(context.current_field_tag) +
                        ", found '" + std::string(1, buffer[0]) + "'",
                    ParseState::ERROR_RECOVERY, 0};
        }

        // Consume the '=' character
        size_t consumed = 1;

        // Transition to parsing the field value
        if (!transitionToState(ParseState::PARSING_VALUE, context))
        {
            return {ParseStatus::StateTransitionError, consumed, nullptr,
                    "Failed to transition to PARSING_VALUE", ParseState::ERROR_RECOVERY, consumed};
        }

        return {ParseStatus::NeedMoreData, consumed, nullptr,
                "Found '=' after field tag " + std::to_string(context.current_field_tag),
                ParseState::PARSING_VALUE, 0};
    }

    StreamFixParser::ParseResult StreamFixParser::handleParsingValue(const char *buffer, size_t length, ParseContext &context)
    {
        // In PARSING_VALUE state, we need to find the SOH delimiter and extract the field value

        // Look for SOH delimiter that marks the end of the field value
        const char *soh_pos = std::find(buffer, buffer + length, FIX_SOH);

        if (soh_pos == buffer + length)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Need more data to find SOH after field value",
                    ParseState::PARSING_VALUE, 0};
        }

        // Calculate the length of the field value (everything before SOH)
        size_t value_length = soh_pos - buffer;

        // Handle empty field values (some FIX fields can be empty)
        if (value_length == 0)
        {
            context.partial_field_value = "";
        }
        else
        {
            // Extract the field value
            std::string field_value(buffer, value_length);
            context.partial_field_value = field_value;
        }

        // Transition to EXPECTING_SOH state (SOH found but not consumed yet)
        if (!transitionToState(ParseState::EXPECTING_SOH, context))
        {
            return {ParseStatus::StateTransitionError, value_length, nullptr,
                    "Failed to transition to EXPECTING_SOH", ParseState::ERROR_RECOVERY, value_length};
        }

        return {ParseStatus::NeedMoreData, value_length, nullptr,
                "Field value parsed for tag " + std::to_string(context.current_field_tag) +
                    ": '" + context.partial_field_value + "'",
                ParseState::EXPECTING_SOH, 0};
    }

    StreamFixParser::ParseResult StreamFixParser::handleExpectingSOH(const char *buffer, size_t length, ParseContext &context)
    {
        // In EXPECTING_SOH state, we should be positioned at the SOH character

        if (length == 0)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Need more data for SOH character",
                    ParseState::EXPECTING_SOH, 0};
        }

        if (buffer[0] != FIX_SOH)
        {
            return {ParseStatus::InvalidFormat, 0, nullptr,
                    "Expected SOH after field " + std::to_string(context.current_field_tag) +
                        "=" + context.partial_field_value + ", found '" + std::string(1, buffer[0]) + "'",
                    ParseState::ERROR_RECOVERY, 0};
        }

        // CRITICAL: Store the completed field
        context.parsed_fields.emplace_back(context.current_field_tag, context.partial_field_value);

        // Update body bytes parsed (tag + "=" + value + SOH)
        context.total_body_bytes_parsed += std::to_string(context.current_field_tag).length() + 1 +
                                           context.partial_field_value.length() + 1;

        size_t consumed = 1; // Consume the SOH

        // Clear current field context for next field
        int stored_tag = context.current_field_tag;             // For logging
        std::string stored_value = context.partial_field_value; // For logging
        context.current_field_tag = 0;
        context.partial_field_value.clear();

        // Determine next state: Are we done with the message body?
        ParseState next_state;
        if (context.total_body_bytes_parsed >= context.expected_body_length)
        {
            // We've parsed all body fields, next should be checksum (10=XXX)
            next_state = ParseState::PARSING_CHECKSUM;
        }
        else
        {
            // More fields to parse in the body
            next_state = ParseState::PARSING_TAG;
        }

        // Transition to the determined next state
        if (!transitionToState(next_state, context))
        {
            return {ParseStatus::StateTransitionError, consumed, nullptr,
                    "Failed to transition to " + std::string(next_state == ParseState::PARSING_CHECKSUM ? "PARSING_CHECKSUM" : "PARSING_TAG"),
                    ParseState::ERROR_RECOVERY, consumed};
        }

        std::string next_state_name = (next_state == ParseState::PARSING_CHECKSUM) ? "PARSING_CHECKSUM" : "PARSING_TAG";
        return {ParseStatus::NeedMoreData, consumed, nullptr,
                "Stored field " + std::to_string(stored_tag) + "='" + stored_value +
                    "', transitioning to " + next_state_name,
                next_state, 0};
    }

    StreamFixParser::ParseResult StreamFixParser::handleParsingChecksum(const char *buffer, size_t length, ParseContext &context)
    {
        // In PARSING_CHECKSUM state, we need to parse the checksum field: 10=XXX\x01

        // Check if we have minimum data for checksum field "10=X\x01" (5 bytes minimum)
        if (length < 5)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Need more data for checksum field",
                    ParseState::PARSING_CHECKSUM, 0};
        }

        // Validate checksum field starts with "10="
        if (buffer[0] != '1' || buffer[1] != '0' || buffer[2] != '=')
        {
            return {ParseStatus::InvalidFormat, 0, nullptr,
                    "Expected checksum field '10=', found '" + std::string(buffer, std::min((size_t)3, length)) + "'",
                    ParseState::ERROR_RECOVERY, 0};
        }

        // Find SOH that terminates the checksum field
        const char *soh_pos = std::find(buffer + 3, buffer + length, FIX_SOH);
        if (soh_pos == buffer + length)
        {
            return {ParseStatus::NeedMoreData, 0, nullptr, "Need more data to find SOH after checksum",
                    ParseState::PARSING_CHECKSUM, 0};
        }

        // Extract checksum value
        size_t checksum_value_length = soh_pos - (buffer + 3);
        if (checksum_value_length == 0)
        {
            return {ParseStatus::InvalidFormat, 3, nullptr, "Empty checksum value",
                    ParseState::ERROR_RECOVERY, 3};
        }

        // Parse checksum as string (like other field values)
        std::string checksum_value(buffer + 3, checksum_value_length);

        // Basic validation - checksum should be 3 digits (001-255)
        if (checksum_value_length != 3)
        {
            return {ParseStatus::InvalidFormat, 3 + checksum_value_length, nullptr,
                    "Invalid checksum format - expected 3 digits, got: '" + checksum_value + "'",
                    ParseState::ERROR_RECOVERY, 3 + checksum_value_length};
        }

        // Validate checksum contains only digits
        for (char c : checksum_value)
        {
            if (c < '0' || c > '9')
            {
                return {ParseStatus::InvalidFormat, 3 + checksum_value_length, nullptr,
                        "Invalid checksum - non-numeric character: '" + checksum_value + "'",
                        ParseState::ERROR_RECOVERY, 3 + checksum_value_length};
            }
        }

        // Store checksum field like any other field
        context.parsed_fields.emplace_back(FixFields::CheckSum, checksum_value);

        // Calculate total bytes consumed (10=XXX\x01)
        size_t consumed = 3 + checksum_value_length + 1; // "10=" + value + SOH

        // Checksum parsed successfully - transition to MESSAGE_COMPLETE state
        if (!transitionToState(ParseState::MESSAGE_COMPLETE, context))
        {
            return {ParseStatus::StateTransitionError, consumed, nullptr,
                    "Failed to transition to MESSAGE_COMPLETE", ParseState::ERROR_RECOVERY, consumed};
        }

        return {ParseStatus::FinishedParsingFields, consumed, nullptr,
                "Checksum parsed, transitioning to MESSAGE_COMPLETE",
                ParseState::MESSAGE_COMPLETE, 0};
    }

    StreamFixParser::ParseResult StreamFixParser::handleMessageComplete(const char *buffer, size_t length, ParseContext &context)
    {
        // Message is complete - allocate and populate the final message
        FixMessage *message = message_pool_->allocate();
        if (!message)
        {
            return {ParseStatus::AllocationFailed, 0, nullptr, "MessagePool allocation failed",
                    ParseState::ERROR_RECOVERY, 0};
        }

        // Populate message with all parsed fields
        for (const auto &field : context.parsed_fields)
        {
            message->setField(field.first, field.second);
        }

        // Set required header fields that were parsed in earlier states
        message->setField(FixFields::BeginString, "FIX.4.4");
        message->setField(FixFields::BodyLength, std::to_string(context.expected_body_length));

        // Extract MsgType from parsed fields (it should be field 35)
        auto msg_type_field = std::find_if(context.parsed_fields.begin(), context.parsed_fields.end(),
                                           [](const std::pair<int, std::string> &field)
                                           {
                                               return field.first == FixFields::MsgType; // 35
                                           });
        if (msg_type_field != context.parsed_fields.end())
        {
            message->setField(FixFields::MsgType, msg_type_field->second);
        }

        // Optional: Validate checksum if enabled
        if (validate_checksum_)
        {
            // Find the checksum field that was just parsed
            auto checksum_field = std::find_if(context.parsed_fields.begin(), context.parsed_fields.end(),
                                               [](const std::pair<int, std::string> &field)
                                               {
                                                   return field.first == FixFields::CheckSum;
                                               });

            if (checksum_field != context.parsed_fields.end())
            {
                // Reconstruct message for checksum calculation (without checksum field)
                std::string message_for_checksum = "8=FIX.4.4";
                message_for_checksum += FIX_SOH;
                message_for_checksum += "9=" + std::to_string(context.expected_body_length);
                message_for_checksum += FIX_SOH;

                // Add all parsed fields except checksum
                for (const auto &field : context.parsed_fields)
                {
                    if (field.first != FixFields::CheckSum)
                    {
                        message_for_checksum += std::to_string(field.first) + "=" + field.second;
                        message_for_checksum += FIX_SOH;
                    }
                }

                // Calculate expected checksum
                uint8_t calculated_checksum = 0;
                for (char c : message_for_checksum)
                {
                    calculated_checksum += static_cast<uint8_t>(c);
                }
                calculated_checksum %= 256;

                // Parse received checksum
                int received_checksum = std::stoi(checksum_field->second);

                // Validate checksums match
                if (calculated_checksum != static_cast<uint8_t>(received_checksum))
                {
                    message_pool_->deallocate(message);
                    return {ParseStatus::ChecksumError, 0, nullptr,
                            "Checksum validation failed: expected " + std::to_string(calculated_checksum) +
                                ", received " + std::to_string(received_checksum),
                            ParseState::ERROR_RECOVERY, 0};
                }
            }
        }

        // Calculate total message length for the return result
        size_t total_message_length = strlen(FIX_BEGIN_STRING) + 1 +                              // BeginString + SOH
                                      std::to_string(context.expected_body_length).length() + 3 + // "9=" + length + SOH
                                      context.expected_body_length +                              // Body
                                      7;                                                          // "10=XXX" + SOH (checksum)

        // Reset context for next message
        context.reset();

        return {ParseStatus::Success, total_message_length, message,
                "Message parsed successfully with " + std::to_string(message->getFieldCount()) + " fields",
                ParseState::IDLE, 0};
    }

    // =================================================================
    // ERROR RECOVERY AND CIRCUIT BREAKER IMPLEMENTATION
    // =================================================================

    StreamFixParser::ParseResult StreamFixParser::handleErrorRecovery(const char *buffer, size_t length, ParseContext &context)
    {
        // Try to skip to next potential FIX message
        size_t skip_bytes = skipToNextPotentialMessage(buffer, length, 0);

        if (skip_bytes >= length)
        {
            // No potential message found in buffer
            stats_.corrupted_data_skipped += length;
            context.reset();
            return {ParseStatus::NeedMoreData, length, nullptr, "Skipped corrupted data, need more",
                    ParseState::IDLE, 0};
        }

        // Found potential message start
        stats_.corrupted_data_skipped += skip_bytes;
        context.reset();

        if (!transitionToState(ParseState::IDLE, context))
        {
            return {ParseStatus::StateTransitionError, skip_bytes, nullptr,
                    "Failed to transition back to IDLE after recovery", ParseState::ERROR_RECOVERY, skip_bytes};
        }

        return {ParseStatus::RecoverySuccess, skip_bytes, nullptr, "Error recovery successful",
                ParseState::IDLE, 0};
    }

    // =================================================================
    // UTILITY FUNCTIONS
    // =================================================================

    bool StreamFixParser::transitionToState(ParseState new_state, ParseContext &context)
    {
        if (!isValidStateTransition(context.current_state, new_state))
        {
            return false;
        }

        updateStateStatistics(context.current_state, new_state);
        context.current_state = new_state;
        return true;
    }

    bool StreamFixParser::isValidStateTransition(ParseState from, ParseState to) const
    {
        // Define valid state transitions
        switch (from)
        {
        case ParseState::IDLE:
            return (to == ParseState::PARSING_BEGIN_STRING || to == ParseState::PARSING_BODY_LENGTH || to == ParseState::ERROR_RECOVERY);

        case ParseState::PARSING_BEGIN_STRING:
            return (to == ParseState::PARSING_BODY_LENGTH || to == ParseState::ERROR_RECOVERY);

        case ParseState::PARSING_BODY_LENGTH:
            return (to == ParseState::PARSING_TAG || to == ParseState::ERROR_RECOVERY);

        case ParseState::PARSING_TAG:
            return (to == ParseState::EXPECTING_EQUALS || to == ParseState::ERROR_RECOVERY);

        case ParseState::EXPECTING_EQUALS:
            return (to == ParseState::PARSING_VALUE || to == ParseState::ERROR_RECOVERY);

        case ParseState::PARSING_VALUE:
            return (to == ParseState::EXPECTING_SOH || to == ParseState::ERROR_RECOVERY);

        case ParseState::EXPECTING_SOH:
            return (to == ParseState::PARSING_TAG || to == ParseState::PARSING_CHECKSUM || to == ParseState::ERROR_RECOVERY);

        case ParseState::PARSING_CHECKSUM:
            return (to == ParseState::MESSAGE_COMPLETE || to == ParseState::ERROR_RECOVERY);

        case ParseState::MESSAGE_COMPLETE:
            return (to == ParseState::IDLE);

        case ParseState::ERROR_RECOVERY:
            return (to == ParseState::IDLE || to == ParseState::CORRUPTED_SKIP);

        case ParseState::CORRUPTED_SKIP:
            return (to == ParseState::IDLE || to == ParseState::ERROR_RECOVERY);

        default:
            return false;
        }
    }

    bool StreamFixParser::isCircuitBreakerActive() const
    {
        return circuit_breaker_active_ || shouldActivateCircuitBreaker(parse_context_);
    }

    bool StreamFixParser::shouldActivateCircuitBreaker(const ParseContext &context) const
    {
        if (!error_recovery_enabled_)
            return false;

        return context.consecutive_errors >= max_consecutive_errors_;
    }

    void StreamFixParser::resetErrorRecovery()
    {
        parse_context_.consecutive_errors = 0;
        parse_context_.last_error_time = std::chrono::steady_clock::now();
        circuit_breaker_active_ = false;
        circuit_breaker_last_reset_ = std::chrono::steady_clock::now();
    }

    bool StreamFixParser::canRecoverFromError(ParseStatus error_status, ParseState current_state)
    {
        if (!error_recovery_enabled_)
            return false;

        // Can recover from format errors and field errors, but not from allocation failures
        return (error_status == ParseStatus::InvalidFormat ||
                error_status == ParseStatus::FieldParseError ||
                error_status == ParseStatus::ChecksumError) &&
               (current_state != ParseState::MESSAGE_COMPLETE);
    }

    size_t StreamFixParser::skipToNextPotentialMessage(const char *buffer, size_t length, size_t start_pos)
    {
        // Look for next occurrence of "8=FIX" pattern
        for (size_t i = start_pos; i < length - 5; ++i)
        {
            if (buffer[i] == '8' && buffer[i + 1] == '=' &&
                buffer[i + 2] == 'F' && buffer[i + 3] == 'I' && buffer[i + 4] == 'X')
            {
                return i;
            }
        }
        return length; // No potential message found
    }

    // =================================================================
    // ENHANCED VALIDATION FUNCTIONS
    // =================================================================

    bool StreamFixParser::validateBeginString(const char *buffer, size_t length, size_t &consumed)
    {
        consumed = 0;

        if (length < strlen(FIX_BEGIN_STRING) + 1) // +1 for SOH
        {
            return false; // Need more data
        }

        // Check if buffer starts with "8=FIX.4.4"
        if (std::strncmp(buffer, FIX_BEGIN_STRING, strlen(FIX_BEGIN_STRING)) != 0)
        {
            return false; // Invalid BeginString
        }

        // Find SOH delimiter
        const char *soh_pos = std::find(buffer + strlen(FIX_BEGIN_STRING), buffer + length, FIX_SOH);
        if (soh_pos == buffer + length)
        {
            return false; // SOH not found - need more data
        }

        consumed = (soh_pos - buffer) + 1; // Include SOH in consumed bytes
        return true;
    }

    bool StreamFixParser::validateBodyLength(const char *buffer, size_t length, ParseContext &context, size_t &consumed)
    {
        consumed = 0;

        // Look for "9=" pattern
        if (length < 3 || buffer[0] != '9' || buffer[1] != '=')
        {
            return false; // Invalid BodyLength format
        }

        // Find SOH after body length value
        const char *soh_pos = std::find(buffer + 2, buffer + length, FIX_SOH);
        if (soh_pos == buffer + length)
        {
            return false; // SOH not found - need more data
        }

        // Parse body length value
        int body_length = 0;
        size_t value_length = soh_pos - (buffer + 2);

        if (!parseInteger(buffer + 2, value_length, body_length) || body_length <= 0)
        {
            return false; // Invalid body length value
        }

        // Validate body length is reasonable
        if (body_length > static_cast<int>(max_message_size_))
        {
            return false; // Body length exceeds maximum
        }

        // Store body length in context
        context.expected_body_length = static_cast<size_t>(body_length);
        consumed = (soh_pos - buffer) + 1; // Include SOH

        return true;
    }

    bool StreamFixParser::isValidFieldTag(int tag)
    {
        // Basic validation - FIX field tags are positive integers
        return tag > 0 && tag <= 99999; // Reasonable upper bound
    }

    bool StreamFixParser::isRequiredField(int tag)
    {
        // Define required FIX fields based on FIX 4.4 specification
        switch (tag)
        {
        case FixFields::BeginString: // 8
        case FixFields::BodyLength:  // 9
        case FixFields::MsgType:     // 35
        case FixFields::CheckSum:    // 10
            return true;
        default:
            return false;
        }
    }

    // =================================================================
    // ENHANCED STATISTICS AND MONITORING
    // =================================================================

    void StreamFixParser::updateStateStatistics(ParseState from, ParseState to)
    {
        stats_.state_transitions++;
        // Could add more detailed transition tracking here
    }

    void StreamFixParser::updateErrorStats(ParseStatus error_status, ParseState error_state)
    {
        stats_.error_frequency[error_status]++;
        stats_.errors_by_state[error_state]++;

        // Update context error tracking
        parse_context_.consecutive_errors++;
        parse_context_.error_count_in_session++;
        parse_context_.last_error_time = std::chrono::steady_clock::now();

        // Track specific error types
        switch (error_status)
        {
        case ParseStatus::FieldParseError:
            stats_.field_parse_errors++;
            break;
        case ParseStatus::ChecksumError:
            stats_.checksum_errors++;
            break;
        case ParseStatus::AllocationFailed:
            stats_.allocation_failures++;
            break;
        default:
            stats_.parse_errors++;
            break;
        }
    }

    void StreamFixParser::recordErrorRecovery(bool successful)
    {
        if (successful)
        {
            stats_.error_recoveries++;
            parse_context_.consecutive_errors = 0; // Reset on successful recovery
        }
    }

    void StreamFixParser::recordStateTransition()
    {
        stats_.state_transitions++;
    }

    void StreamFixParser::recordPartialMessageHandled()
    {
        stats_.partial_messages_handled++;
    }

    double StreamFixParser::getErrorRate() const
    {
        if (stats_.messages_parsed == 0)
            return 0.0;

        return static_cast<double>(stats_.parse_errors) / static_cast<double>(stats_.messages_parsed) * 100.0;
    }

    StreamFixParser::ParseResult StreamFixParser::attemptErrorRecovery(const char *buffer, size_t length,
                                                                       ParseContext &context,
                                                                       const std::string &error_detail)
    {
        // Transition to error recovery state
        if (!transitionToState(ParseState::ERROR_RECOVERY, context))
        {
            return {ParseStatus::StateTransitionError, 0, nullptr,
                    "Failed to enter error recovery state: " + error_detail,
                    context.current_state, 0};
        }

        // Handle error recovery
        return handleErrorRecovery(buffer, length, context);
    }

    StreamFixParser::ParseResult StreamFixParser::handleCorruptedSkip(const char *buffer, size_t length, ParseContext &context)
    {
        // Skip corrupted data and try to find next message
        return handleErrorRecovery(buffer, length, context);
    }

} // namespace fix_gateway::protocol

// =================================================================
// TEMPLATE SPECIALIZATIONS - OPTIMIZED PARSING (Phase 2C)
// =================================================================

namespace fix_gateway::protocol
{
    // Helper function implementation
    std::string_view StreamParserUtils::extractMsgType(const char *buffer, size_t length)
    {
        // Find "35=" pattern and extract value
        const char *msg_type_pos = std::search(buffer, buffer + length, "35=", &"35="[3]);
        if (msg_type_pos == buffer + length)
        {
            return std::string_view{}; // Not found
        }

        const char *value_start = msg_type_pos + 3; // Skip "35="
        const char *soh_pos = static_cast<const char *>(memchr(value_start, '\001', (buffer + length) - value_start));
        if (!soh_pos)
        {
            return std::string_view{};
        }

        return std::string_view{value_start, static_cast<size_t>(soh_pos - value_start)};
    }

    // Intelligent parsing implementation - framework for future optimization
    StreamFixParser::ParseResult StreamFixParser::parseIntelligent(const char *buffer, size_t length)
    {
        std::string_view msg_type = StreamParserUtils::extractMsgType(buffer, length);

        if (msg_type.empty())
        {
            return parse(buffer, length);
        }

        // Message type detection successful - dispatch to optimized parsers

        // Template-optimized parsing for performance-critical message types (INCOMING MESSAGES ONLY)
        // Note: NEW_ORDER_SINGLE removed - we don't receive these from exchange/broker

        // Template-optimized parsing for incoming message types
        if (msg_type == "8") // EXECUTION_REPORT
        {
            return OptimizedParser<FixMsgType::EXECUTION_REPORT>::parseExecutionReport(this, buffer, length);
        }
        else if (msg_type == "0") // HEARTBEAT
        {
            return OptimizedParser<FixMsgType::HEARTBEAT>::parseHeartbeat(this, buffer, length);
        }

        // Fall back to legacy parseMessage for all other message types
        // Note: Don't call parse() here to avoid infinite recursion
        size_t message_start = 0;
        size_t message_end = length;
        return parseMessage(buffer, message_start, message_end);
    }

} // namespace fix_gateway::protocol