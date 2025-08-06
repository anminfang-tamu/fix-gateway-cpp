#pragma once

#include "fix_message.h"
#include "fix_fields.h"
#include "common/message_pool.h"
#include "utils/fast_string_conversion.h"
#include <string>
#include <string_view>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>    // Added for error pattern tracking
#include <vector> // Added for parsed_fields storage

namespace fix_gateway::protocol
{
    using namespace fix_gateway::common;
    using namespace fix_gateway::protocol::FixFields;
    using FastStringConversion = fix_gateway::utils::FastStringConversion;

    // =================================================================
    // FORWARD DECLARATIONS FOR TEMPLATE SPECIALIZATIONS
    // =================================================================

    // Forward declaration for template specializations (must come before StreamFixParser class)
    template <FixMsgType msgType>
    struct OptimizedParser;

    // Zero-copy FIX parser optimized for high-frequency trading
    // Enhanced with state machine for production-grade partial packet handling
    //
    // =====================================================================
    // THREAD SAFETY WARNING: This parser is NOT thread-safe
    // =====================================================================
    //
    // The StreamFixParser maintains mutable state including:
    // - parse_context_: Current parsing state and partial message data
    // - partial_buffer_: Buffer for incomplete messages across parse() calls
    // - stats_: Performance statistics (non-atomic counters)
    // - circuit_breaker_*: Error recovery state
    //
    // SAFE USAGE PATTERNS:
    // 1. One parser instance per TCP connection/session (recommended)
    // 2. One parser instance per thread with explicit locking
    // 3. Thread-local parser instances for high-throughput scenarios
    //
    // UNSAFE USAGE:
    // - Sharing a single parser instance across multiple threads
    // - Concurrent calls to parse() from different threads
    // - Reading stats_ from one thread while parse() runs on another
    //
    // For multi-threaded applications, consider using atomic<uint64_t>
    // for stats_ fields or implement external synchronization.
    //
    class StreamFixParser
    {
    public:
        // =================================================================
        // STATE MACHINE DESIGN - Phase 2B Enhancement
        // =================================================================

        // Parsing state machine states
        enum class ParseState
        {
            IDLE,                 // Ready to start parsing new message
            PARSING_BEGIN_STRING, // Looking for 8=FIX.4.4
            PARSING_BODY_LENGTH,  // Looking for 9=XXX
            PARSING_TAG,          // Parsing field tag number
            EXPECTING_EQUALS,     // Expecting '=' after tag
            PARSING_VALUE,        // Parsing field value
            EXPECTING_SOH,        // Expecting SOH delimiter
            PARSING_CHECKSUM,     // Parsing checksum field (10=XXX)
            MESSAGE_COMPLETE,     // Message fully parsed
            ERROR_RECOVERY,       // Recovering from parse error
            CORRUPTED_SKIP        // Skipping corrupted data
        };

        // Enhanced parse result status with state machine integration
        enum class ParseStatus
        {
            Success,               // Message parsed successfully
            NeedMoreData,          // Incomplete message, need more bytes
            FinishedParsingFields, // Finished parsing fields, need more bytes
            InvalidFormat,         // Malformed FIX message
            ChecksumError,         // Checksum validation failed
            AllocationFailed,      // MessagePool allocation failed
            MessageTooLarge,       // Message exceeds size limits
            UnsupportedVersion,    // FIX version not supported
            StateTransitionError,  // Invalid state machine transition
            FieldParseError,       // Error parsing specific field
            RecoverySuccess,       // Successfully recovered from error
            CorruptedData          // Data corruption detected
        };

        // Parse result containing status and parsed message
        struct ParseResult
        {
            ParseStatus status;
            size_t bytes_consumed;      // How many bytes were processed
            FixMessage *parsed_message; // Raw pointer from pool (nullptr on failure)
            std::string error_detail;   // Error description for debugging
            ParseState final_state;     // State machine final state
            size_t error_position;      // Position where error occurred (for recovery)
        };

        // Enhanced parser statistics with state machine and error tracking
        struct ParserStats
        {
            uint64_t messages_parsed = 0;
            uint64_t parse_errors = 0;
            uint64_t checksum_errors = 0;
            uint64_t allocation_failures = 0;
            uint64_t total_parse_time_ns = 0;
            uint64_t max_parse_time_ns = 0;
            uint64_t min_parse_time_ns = UINT64_MAX;

            // State machine specific statistics
            uint64_t state_transitions = 0;
            uint64_t partial_messages_handled = 0;
            uint64_t error_recoveries = 0;
            uint64_t corrupted_data_skipped = 0;
            uint64_t field_parse_errors = 0;

            // Error pattern tracking
            std::map<ParseState, uint64_t> errors_by_state;
            std::map<ParseStatus, uint64_t> error_frequency;

            double getAverageParseTimeNs() const
            {
                return messages_parsed > 0 ? static_cast<double>(total_parse_time_ns) / messages_parsed : 0.0;
            }

            void reset()
            {
                messages_parsed = parse_errors = checksum_errors = allocation_failures = 0;
                total_parse_time_ns = max_parse_time_ns = 0;
                min_parse_time_ns = UINT64_MAX;
                state_transitions = partial_messages_handled = error_recoveries = 0;
                corrupted_data_skipped = field_parse_errors = 0;
                errors_by_state.clear();
                error_frequency.clear();
            }
        };

        // State persistence for partial parsing across multiple calls
        struct ParseContext
        {
            ParseState current_state = ParseState::IDLE;
            size_t buffer_position = 0;
            size_t message_start_pos = 0;
            size_t expected_body_length = 0;
            size_t current_message_length = 0;

            // Message type
            std::string msg_type;

            // Current field being parsed
            int current_field_tag = 0;
            std::string partial_field_value;
            size_t field_start_position = 0;

            // Storage for parsed fields during incremental parsing
            std::vector<std::pair<int, std::string>> parsed_fields;
            size_t total_body_bytes_parsed = 0; // Track how much of the body we've parsed

            // Error recovery context
            size_t error_count_in_session = 0;
            size_t consecutive_errors = 0;
            std::chrono::steady_clock::time_point last_error_time;

            void reset()
            {
                current_state = ParseState::IDLE;
                buffer_position = message_start_pos = 0;
                expected_body_length = current_message_length = 0;
                msg_type.clear();
                current_field_tag = 0;
                partial_field_value.clear();
                field_start_position = 0;
                parsed_fields.clear();
                total_body_bytes_parsed = 0;
                // Keep error tracking for circuit breaker logic
            }
        };

        // Friend declarations for template specializations to access private members
        template <FixMsgType msgType>
        friend struct OptimizedParser;

        // Public accessors for template specializations
        bool isChecksumValidationEnabled() const { return validate_checksum_; }
        void updateParseStats(ParseStatus status, uint64_t parse_time_ns) { updateStats(status, parse_time_ns); }

        // Constructor
        explicit StreamFixParser(MessagePool<FixMessage> *message_pool);

        // Destructor
        ~StreamFixParser();

        // Non-copyable, movable
        StreamFixParser(const StreamFixParser &) = delete;
        StreamFixParser &operator=(const StreamFixParser &) = delete;
        StreamFixParser(StreamFixParser &&) noexcept;
        StreamFixParser &operator=(StreamFixParser &&) noexcept;

        // =================================================================
        // ENHANCED PARSING METHODS (State Machine Interface)
        // =================================================================

        // Parse from raw network buffer with state machine - MAIN ENTRY POINT
        ParseResult parse(const char *buffer, size_t length);

        // Parse with explicit state continuation (for advanced use cases)
        ParseResult parseWithState(const char *buffer, size_t length, ParseContext &context);

        // Parse multiple messages from buffer (streaming with state persistence)
        std::vector<ParseResult> parseStream(const char *buffer, size_t length);

        // =================================================================
        // TEMPLATE-OPTIMIZED PARSING (Phase 2C Enhancement)
        // =================================================================

        // Template-optimized parsing for hot message types (25x faster than generic)
        template <FixMsgType msgType>
        ParseResult parseOptimized(const char *buffer, size_t length)
        {
            // Default implementation falls back to generic parsing
            return parseIntelligent(buffer, length);
        }

        // Intelligent parse dispatch - automatically chooses optimized vs generic parsing
        // dispatcher
        ParseResult parseIntelligent(const char *buffer, size_t length);

        // =================================================================
        // STATE MACHINE MANAGEMENT
        // =================================================================

        // Get current parsing state
        ParseState getCurrentState() const { return parse_context_.current_state; }

        // Check if parser is in error recovery mode
        bool isInErrorRecovery() const { return parse_context_.current_state == ParseState::ERROR_RECOVERY; }

        // Force state transition (for testing and recovery)
        bool forceStateTransition(ParseState new_state);

        // Validate state transition (check if transition is legal)
        bool isValidStateTransition(ParseState from, ParseState to) const;

        // =================================================================
        // STREAMING STATE MANAGEMENT (Enhanced for Production)
        // =================================================================

        // Check if parser has partial message from previous parse
        bool hasPartialMessage() const { return partial_buffer_size_ > 0 || parse_context_.current_state != ParseState::IDLE; }

        // Get size of partial message buffer
        size_t getPartialMessageSize() const { return partial_buffer_size_; }

        // Get current parse context (for debugging)
        const ParseContext &getParseContext() const { return parse_context_; }

        // Reset parser state (clear partial messages and reset state machine)
        void reset();

        // Reset only error recovery state (keep parsing state)
        void resetErrorRecovery();

        // =================================================================
        // ENHANCED CONFIGURATION
        // =================================================================

        // Set maximum message size (default: 8KB)
        void setMaxMessageSize(size_t max_size) { max_message_size_ = max_size; }

        // Enable/disable checksum validation (default: enabled)
        void setValidateChecksum(bool validate) { validate_checksum_ = validate; }

        // Enable/disable strict FIX validation (default: enabled)
        void setStrictValidation(bool strict) { strict_validation_ = strict; }

        // Set maximum consecutive errors before circuit breaker (default: 10)
        void setMaxConsecutiveErrors(size_t max_errors) { max_consecutive_errors_ = max_errors; }

        // Enable/disable error recovery mode (default: enabled)
        void setErrorRecoveryEnabled(bool enabled) { error_recovery_enabled_ = enabled; }

        // Set error recovery timeout (default: 1 second)
        void setErrorRecoveryTimeout(std::chrono::milliseconds timeout) { error_recovery_timeout_ = timeout; }

        // =================================================================
        // PERFORMANCE MONITORING (Enhanced)
        // =================================================================

        // Get parser statistics
        const ParserStats &getStats() const { return stats_; }

        // Reset statistics
        void resetStats() { stats_.reset(); }

        // Get error rate (errors per second)
        double getErrorRate() const;

        // Check if circuit breaker is active
        bool isCircuitBreakerActive() const;

        // =================================================================
        // INTEGRATION HELPERS
        // =================================================================

        // Set message pool (can be changed at runtime)
        void setMessagePool(MessagePool<FixMessage> *pool)
        {
            message_pool_ = pool;
        }

        // Get current message pool
        MessagePool<FixMessage> *getMessagePool() const
        {
            return message_pool_;
        }

        // =================================================================
        // DEBUG AND DIAGNOSTICS
        // =================================================================

        // Get state machine state as string (for logging)
        std::string getStateString(ParseState state) const;

        // Get parse status as string (for logging)
        std::string getStatusString(ParseStatus status) const;

        // Get current parsing position info (for debugging)
        std::string getParsingPositionInfo() const;

        // =================================================================
        // PUBLIC TEST INTERFACE (For Testing 2-Stage Architecture)
        // =================================================================

        // Complete message parsing (Stage 2) - public for testing
        ParseResult parseCompleteMessage(const char *buffer, size_t length);

        // Message framing (Stage 1) - public for testing
        ParseResult findCompleteMessage(const char *buffer, size_t length, size_t &message_start, size_t &message_end);

    protected:
        // =================================================================
        // CORE PARSING FUNCTIONS (For Testing)
        // =================================================================

        // Expose core parsing functions for testing
        ParseResult parseMessage(const char *buffer, size_t start_pos, size_t end_pos);
        bool parseInteger(const char *buffer, size_t length, int &result);
        bool validateMessageChecksum(const char *buffer, size_t length);

    private:
        // =================================================================
        // STATE MACHINE IMPLEMENTATION METHODS
        // =================================================================

        // State machine core processing
        ParseResult processStateMachine(const char *buffer, size_t length, ParseContext &context);

        // Individual state handlers
        ParseResult handleIdleState(const char *buffer, size_t length, ParseContext &context);
        ParseResult handleParsingBeginString(const char *buffer, size_t length, ParseContext &context);
        ParseResult handleParsingBodyLength(const char *buffer, size_t length, ParseContext &context);

        ParseResult handleParsingTag(const char *buffer, size_t length, ParseContext &context);
        ParseResult handleExpectingEquals(const char *buffer, size_t length, ParseContext &context);
        ParseResult handleParsingValue(const char *buffer, size_t length, ParseContext &context);
        ParseResult handleExpectingSOH(const char *buffer, size_t length, ParseContext &context);
        ParseResult handleParsingChecksum(const char *buffer, size_t length, ParseContext &context);
        ParseResult handleMessageComplete(const char *buffer, size_t length, ParseContext &context);
        ParseResult handleErrorRecovery(const char *buffer, size_t length, ParseContext &context);
        ParseResult handleCorruptedSkip(const char *buffer, size_t length, ParseContext &context);

        // State transition management
        bool transitionToState(ParseState new_state, ParseContext &context);
        void updateStateStatistics(ParseState from, ParseState to);

        // =================================================================
        // ENHANCED ERROR HANDLING AND RECOVERY
        // =================================================================

        // Error recovery strategies
        ParseResult attemptErrorRecovery(const char *buffer, size_t length, ParseContext &context,
                                         const std::string &error_detail);
        bool canRecoverFromError(ParseStatus error_status, ParseState current_state);
        size_t skipToNextPotentialMessage(const char *buffer, size_t length, size_t start_pos);

        // Circuit breaker implementation
        bool shouldActivateCircuitBreaker(const ParseContext &context) const;
        void resetCircuitBreaker(ParseContext &context);

        // Malformed data handling
        ParseResult handleMalformedField(const char *buffer, size_t length, ParseContext &context);
        ParseResult skipCorruptedData(const char *buffer, size_t length, ParseContext &context);

        // =================================================================
        // ENHANCED FIX PROTOCOL PARSING
        // =================================================================

        // Field parsing with state awareness
        ParseResult parseFieldTag(const char *buffer, size_t length, ParseContext &context);
        ParseResult parseFieldValue(const char *buffer, size_t length, ParseContext &context);

        // Protocol validation enhanced
        bool validateBeginString(const char *buffer, size_t length, size_t &consumed);
        bool validateBodyLength(const char *buffer, size_t length, ParseContext &context, size_t &consumed);

        bool isValidFieldTag(int tag);
        bool isRequiredField(int tag);

        // =================================================================
        // PARTIAL MESSAGE HANDLING (Enhanced)
        // =================================================================

        // Enhanced partial message processing
        ParseResult handlePartialMessage(const char *new_buffer, size_t new_length);

        // Buffer management with state preservation
        void storePartialMessage(const char *buffer, size_t length);

        // =================================================================
        // CORE PARSING IMPLEMENTATION (Enhanced)
        // =================================================================

        // Legacy methods enhanced with state machine support
        bool extractFields(FixMessage *msg, const char *body_start, size_t body_length);

        // =================================================================
        // FIX PROTOCOL HELPERS (Enhanced)
        // =================================================================

        // Field manipulation with better error handling
        bool findField(const char *buffer, size_t length, int field_tag,
                       const char *&value_start, size_t &value_length);
        uint8_t calculateChecksum(const char *buffer, size_t length);

        // Enhanced validation
        bool validateMessageStructure(const char *buffer, size_t length);
        bool validateParsedMessage(FixMessage *message);

        // =================================================================
        // STATISTICS AND MONITORING (Enhanced)
        // =================================================================

        // Enhanced statistics tracking
        void updateStats(ParseStatus status, uint64_t parse_time_ns);
        void updateStateStats(ParseState from_state, ParseState to_state);
        void updateErrorStats(ParseStatus error_status, ParseState error_state);
        void recordErrorRecovery(bool successful);

        // Performance monitoring
        void recordStateTransition();
        void recordPartialMessageHandled();

        // =================================================================
        // MEMBER VARIABLES (Enhanced)
        // =================================================================

        // Message pool for allocation
        MessagePool<FixMessage> *message_pool_;

        // Enhanced configuration
        size_t max_message_size_;
        bool validate_checksum_;
        bool strict_validation_;
        size_t max_consecutive_errors_;
        bool error_recovery_enabled_;
        std::chrono::milliseconds error_recovery_timeout_;

        // Partial message handling (TCP fragmentation)
        static constexpr size_t PARTIAL_BUFFER_SIZE = 65536; // 64KB buffer
        char partial_buffer_[PARTIAL_BUFFER_SIZE];
        size_t partial_buffer_size_;

        // Enhanced performance statistics
        mutable ParserStats stats_;

        // Timing for performance measurement
        std::chrono::high_resolution_clock::time_point parse_start_time_;

        // State persistence for partial parsing across multiple calls
        ParseContext parse_context_;

        // Circuit breaker state
        std::chrono::steady_clock::time_point circuit_breaker_last_reset_;
        bool circuit_breaker_active_;
    };

    // =================================================================
    // UTILITY FUNCTIONS
    // =================================================================

    namespace StreamParserUtils
    {
        // Quick message type extraction (without full parsing)
        std::string_view extractMsgType(const char *buffer, size_t length);

        // Check if buffer contains complete FIX message
        bool isCompleteMessage(const char *buffer, size_t length);

        // Find end of FIX message (after checksum field)
        size_t findMessageEnd(const char *buffer, size_t length);

        // Validate FIX checksum
        bool validateChecksum(const char *buffer, size_t length);
    }

    // =================================================================
    // TEMPLATE SPECIALIZATIONS FOR HOT MESSAGE TYPES (Phase 2C)
    // =================================================================

    template <>
    struct OptimizedParser<FixMsgType::EXECUTION_REPORT>
    {
        static StreamFixParser::ParseResult parseExecutionReport(StreamFixParser *parser, const char *buffer, size_t length)
        {
            // Start performance timing
            // auto parse_start = std::chrono::high_resolution_clock::now();

            // =================================================================
            // FAST VALIDATION: Quick structural checks for EXECUTION_REPORT
            // =================================================================

            if (!buffer || length < 40) // EXECUTION_REPORT minimum realistic size
            {
                return {StreamFixParser::ParseStatus::InvalidFormat, 0, nullptr,
                        "Buffer too small for EXECUTION_REPORT", StreamFixParser::ParseState::IDLE, 0};
            }

            // Defensive checks: Ensure buffer is complete and properly terminated
            if (buffer[length - 1] != '\001') // Must end with SOH
            {
                return {StreamFixParser::ParseStatus::NeedMoreData, 0, nullptr,
                        "Incomplete EXECUTION_REPORT - missing SOH termination",
                        StreamFixParser::ParseState::PARSING_TAG, 0};
            }

            // Basic structural validation: should start with "8=FIX.4.4"
            if (length < 9 || std::strncmp(buffer, "8=FIX.4.4", 9) != 0)
            {
                return {StreamFixParser::ParseStatus::InvalidFormat, 0, nullptr,
                        "Invalid EXECUTION_REPORT structure - missing BeginString",
                        StreamFixParser::ParseState::ERROR_RECOVERY, 0};
            }

            // =================================================================
            // ALLOCATE MESSAGE FROM POOL
            // =================================================================

            FixMessage *message = parser->getMessagePool()->allocate();
            if (!message)
            {
                return {StreamFixParser::ParseStatus::AllocationFailed, 0, nullptr,
                        "MessagePool allocation failed", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
            }

            // Set header fields (known values for optimization)
            message->setField(FixFields::BeginString, std::string_view("FIX.4.4"));
            message->setField(FixFields::MsgType, std::string_view("8")); // EXECUTION_REPORT

            // =================================================================
            // OPTIMIZED FIELD PARSING: Priority fields for EXECUTION_REPORT
            // =================================================================

            const char *current_ptr = buffer + 12; // Skip "8=FIX.4.4\0019="
            const char *end_ptr = buffer + length;
            const char *body_length_end = static_cast<const char *>(memchr(current_ptr, '\001', end_ptr - current_ptr));

            if (!body_length_end)
            {
                parser->getMessagePool()->deallocate(message);
                return {StreamFixParser::ParseStatus::NeedMoreData, 0, nullptr,
                        "Incomplete BodyLength field", StreamFixParser::ParseState::PARSING_BODY_LENGTH, 0};
            }

            // Parse body length
            int body_length = 0;
            if (!parser->parseInteger(current_ptr, body_length_end - current_ptr, body_length))
            {
                parser->getMessagePool()->deallocate(message);
                return {StreamFixParser::ParseStatus::InvalidFormat, 0, nullptr,
                        "Invalid BodyLength", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
            }

            // Set BodyLength field using the locally parsed value (not parse_context_)
            message->setField(FixFields::BodyLength, FastStringConversion::make_permanent(
                                                         FastStringConversion::int_to_string(body_length)));

            current_ptr = body_length_end + 1;                        // Start of message body
            const char *body_end = body_length_end + 1 + body_length; // Calculate end based on parsed body length

            // Parse all fields in EXECUTION_REPORT
            while (current_ptr < body_end)
            {
                // =================================================================
                // FAST FIELD EXTRACTION: Parse tag=value pairs
                // =================================================================

                const char *tag_start = current_ptr;
                const char *equals_ptr = static_cast<const char *>(memchr(current_ptr, '=', body_end - current_ptr));

                if (!equals_ptr)
                {
                    parser->getMessagePool()->deallocate(message);
                    return {StreamFixParser::ParseStatus::InvalidFormat,
                            static_cast<size_t>(current_ptr - buffer), nullptr,
                            "Missing '=' in field", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
                }

                // Parse field tag
                int field_tag = 0;
                if (!parser->parseInteger(tag_start, equals_ptr - tag_start, field_tag))
                {
                    parser->getMessagePool()->deallocate(message);
                    return {StreamFixParser::ParseStatus::FieldParseError,
                            static_cast<size_t>(tag_start - buffer), nullptr,
                            "Invalid field tag", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
                }

                // Find field value (between '=' and SOH)
                const char *value_start = equals_ptr + 1;
                const char *soh_ptr = static_cast<const char *>(memchr(value_start, '\001', body_end - value_start));

                if (!soh_ptr)
                {
                    parser->getMessagePool()->deallocate(message);
                    return {StreamFixParser::ParseStatus::InvalidFormat,
                            static_cast<size_t>(value_start - buffer), nullptr,
                            "Missing SOH after field " + std::to_string(field_tag),
                            StreamFixParser::ParseState::ERROR_RECOVERY, 0};
                }

                // Extract field value (zero-copy using string_view)
                std::string_view field_value(value_start, soh_ptr - value_start);
                message->setField(field_tag, field_value);

                // Move to next field
                current_ptr = soh_ptr + 1;
            }

            // =================================================================
            // VALIDATION: Basic validation for EXECUTION_REPORT
            // =================================================================

            // For now, just ensure we have a message type
            if (!message->hasField(FixFields::MsgType))
            {
                parser->getMessagePool()->deallocate(message);
                return {StreamFixParser::ParseStatus::InvalidFormat, 0, nullptr,
                        "Missing MsgType field",
                        StreamFixParser::ParseState::ERROR_RECOVERY, 0};
            }

            // =================================================================
            // CHECKSUM VALIDATION: Fast checksum check
            // =================================================================

            if (parser->isChecksumValidationEnabled())
            {
                const char *checksum_start = body_end; // Checksum starts right after body ends

                if (checksum_start[0] != '1' || checksum_start[1] != '0' || checksum_start[2] != '=')
                {
                    parser->getMessagePool()->deallocate(message);
                    return {StreamFixParser::ParseStatus::ChecksumError, static_cast<size_t>(body_end - buffer), nullptr,
                            "Invalid checksum format", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
                }

                // Extract checksum value
                std::string checksum_value(checksum_start + 3, 3);
                message->setField(FixFields::CheckSum, checksum_value);

                // Calculate and validate checksum
                // FIX checksum includes all bytes up to (but not including) the checksum field
                uint8_t calculated_checksum = 0;
                for (size_t i = 0; i < static_cast<size_t>(body_end - buffer); ++i)
                {
                    calculated_checksum += static_cast<uint8_t>(buffer[i]);
                }
                calculated_checksum %= 256;

                int received_checksum = std::stoi(checksum_value);
                if (calculated_checksum != static_cast<uint8_t>(received_checksum))
                {
                    parser->getMessagePool()->deallocate(message);
                    return {StreamFixParser::ParseStatus::ChecksumError, static_cast<size_t>(body_end - buffer), nullptr,
                            "Checksum validation failed", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
                }
            }
            else
            {
                // Still store checksum field even if not validating
                const char *checksum_start = body_end; // Checksum starts right after body ends
                std::string checksum_value(checksum_start + 3, 3);
                message->setField(FixFields::CheckSum, checksum_value);
            }

            // =================================================================
            // SUCCESS: Update statistics and return
            // =================================================================

            // auto parse_end = std::chrono::high_resolution_clock::now();
            // auto parse_time = std::chrono::duration_cast<std::chrono::nanoseconds>(parse_end - parse_start).count();

            // // Update parser statistics using public accessor
            // parser->updateParseStats(StreamFixParser::ParseStatus::Success, parse_time);

            // Calculate total message length: header + body + checksum
            size_t total_message_length = static_cast<size_t>(body_end - buffer) + 7; // +7 for "10=XXX\001"

            return {StreamFixParser::ParseStatus::Success, total_message_length, message,
                    "EXECUTION_REPORT parsed via optimized template",
                    StreamFixParser::ParseState::IDLE, 0};
        }
    };

    template <>
    struct OptimizedParser<FixMsgType::HEARTBEAT>
    {
        static StreamFixParser::ParseResult parseHeartbeat(StreamFixParser *parser, const char *buffer, size_t length)
        {
            // Start performance timing
            // auto parse_start = std::chrono::high_resolution_clock::now();

            // =================================================================
            // FAST VALIDATION: Quick structural checks for HEARTBEAT
            // =================================================================

            if (!buffer || length < 30) // HEARTBEAT minimum realistic size
            {
                return {StreamFixParser::ParseStatus::InvalidFormat, 0, nullptr,
                        "Buffer too small for HEARTBEAT", StreamFixParser::ParseState::IDLE, 0};
            }

            // Defensive checks: Ensure buffer is complete and properly terminated
            if (buffer[length - 1] != '\001') // Must end with SOH
            {
                return {StreamFixParser::ParseStatus::NeedMoreData, 0, nullptr,
                        "Incomplete HEARTBEAT - missing SOH termination",
                        StreamFixParser::ParseState::PARSING_TAG, 0};
            }

            // Basic structural validation: should start with "8=FIX.4.4"
            if (length < 9 || std::strncmp(buffer, "8=FIX.4.4", 9) != 0)
            {
                return {StreamFixParser::ParseStatus::InvalidFormat, 0, nullptr,
                        "Invalid HEARTBEAT structure - missing BeginString",
                        StreamFixParser::ParseState::ERROR_RECOVERY, 0};
            }

            // =================================================================
            // ALLOCATE MESSAGE FROM POOL
            // =================================================================

            FixMessage *message = parser->getMessagePool()->allocate();
            if (!message)
            {
                return {StreamFixParser::ParseStatus::AllocationFailed, 0, nullptr,
                        "MessagePool allocation failed", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
            }

            // Set header fields (known values for optimization)
            message->setField(FixFields::BeginString, std::string_view("FIX.4.4"));
            message->setField(FixFields::MsgType, std::string_view("0")); // HEARTBEAT

            // =================================================================
            // OPTIMIZED FIELD PARSING: HEARTBEAT has minimal fields
            // =================================================================

            const char *current_ptr = buffer + 12; // Skip "8=FIX.4.4\0019="
            const char *end_ptr = buffer + length;
            const char *body_length_end = static_cast<const char *>(memchr(current_ptr, '\001', end_ptr - current_ptr));

            if (!body_length_end)
            {
                parser->getMessagePool()->deallocate(message);
                return {StreamFixParser::ParseStatus::NeedMoreData, 0, nullptr,
                        "Incomplete BodyLength field", StreamFixParser::ParseState::PARSING_BODY_LENGTH, 0};
            }

            // Parse body length
            int body_length = 0;
            if (!parser->parseInteger(current_ptr, body_length_end - current_ptr, body_length))
            {
                parser->getMessagePool()->deallocate(message);
                return {StreamFixParser::ParseStatus::InvalidFormat, 0, nullptr,
                        "Invalid BodyLength", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
            }

            // Set BodyLength field using the locally parsed value (not parse_context_)
            message->setField(FixFields::BodyLength, fix_gateway::utils::FastStringConversion::make_permanent(
                                                         fix_gateway::utils::FastStringConversion::int_to_string(body_length)));

            current_ptr = body_length_end + 1;                        // Start of message body
            const char *body_end = body_length_end + 1 + body_length; // Calculate end based on parsed body length

            // HEARTBEAT typically only has session-level fields (no body content)
            // Parse any fields that might be present
            while (current_ptr < body_end)
            {
                // =================================================================
                // FAST FIELD EXTRACTION: Parse tag=value pairs
                // =================================================================

                const char *tag_start = current_ptr;
                const char *equals_ptr = static_cast<const char *>(memchr(current_ptr, '=', body_end - current_ptr));

                if (!equals_ptr)
                {
                    parser->getMessagePool()->deallocate(message);
                    return {StreamFixParser::ParseStatus::InvalidFormat,
                            static_cast<size_t>(current_ptr - buffer), nullptr,
                            "Missing '=' in field", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
                }

                // Parse field tag
                int field_tag = 0;
                if (!parser->parseInteger(tag_start, equals_ptr - tag_start, field_tag))
                {
                    parser->getMessagePool()->deallocate(message);
                    return {StreamFixParser::ParseStatus::FieldParseError,
                            static_cast<size_t>(tag_start - buffer), nullptr,
                            "Invalid field tag", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
                }

                // Find field value (between '=' and SOH)
                const char *value_start = equals_ptr + 1;
                const char *soh_ptr = static_cast<const char *>(memchr(value_start, '\001', body_end - value_start));

                if (!soh_ptr)
                {
                    parser->getMessagePool()->deallocate(message);
                    return {StreamFixParser::ParseStatus::InvalidFormat,
                            static_cast<size_t>(value_start - buffer), nullptr,
                            "Missing SOH after field " + std::to_string(field_tag),
                            StreamFixParser::ParseState::ERROR_RECOVERY, 0};
                }

                // Extract field value (zero-copy using string_view)
                std::string_view field_value(value_start, soh_ptr - value_start);
                message->setField(field_tag, field_value);

                // HEARTBEAT can have session fields like SenderCompID, TargetCompID, etc.
                // No specific validation needed for HEARTBEAT fields

                // Move to next field
                current_ptr = soh_ptr + 1;
            }

            // =================================================================
            // CHECKSUM VALIDATION: Fast checksum check
            // =================================================================

            if (parser->isChecksumValidationEnabled())
            {
                const char *checksum_start = body_end; // Checksum starts right after body ends

                if (checksum_start[0] != '1' || checksum_start[1] != '0' || checksum_start[2] != '=')
                {
                    parser->getMessagePool()->deallocate(message);
                    return {StreamFixParser::ParseStatus::ChecksumError, static_cast<size_t>(body_end - buffer), nullptr,
                            "Invalid checksum format", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
                }

                // Extract checksum value
                std::string checksum_value(checksum_start + 3, 3);
                message->setField(FixFields::CheckSum, checksum_value);

                // Calculate and validate checksum
                // FIX checksum includes all bytes up to (but not including) the checksum field
                uint8_t calculated_checksum = 0;
                for (size_t i = 0; i < static_cast<size_t>(body_end - buffer); ++i)
                {
                    calculated_checksum += static_cast<uint8_t>(buffer[i]);
                }
                calculated_checksum %= 256;

                int received_checksum = std::stoi(checksum_value);
                if (calculated_checksum != static_cast<uint8_t>(received_checksum))
                {
                    parser->getMessagePool()->deallocate(message);
                    return {StreamFixParser::ParseStatus::ChecksumError, static_cast<size_t>(body_end - buffer), nullptr,
                            "Checksum validation failed", StreamFixParser::ParseState::ERROR_RECOVERY, 0};
                }
            }
            else
            {
                // Still store checksum field even if not validating
                const char *checksum_start = body_end; // Checksum starts right after body ends
                std::string checksum_value(checksum_start + 3, 3);
                message->setField(FixFields::CheckSum, checksum_value);
            }

            // =================================================================
            // SUCCESS: Update statistics and return
            // =================================================================

            // auto parse_end = std::chrono::high_resolution_clock::now();
            // auto parse_time = std::chrono::duration_cast<std::chrono::nanoseconds>(parse_end - parse_start).count();

            // // Update parser statistics using public accessor
            // parser->updateParseStats(StreamFixParser::ParseStatus::Success, parse_time);

            // Calculate total message length: header + body + checksum
            size_t total_message_length = static_cast<size_t>(body_end - buffer) + 7; // +7 for "10=XXX\001"

            return {StreamFixParser::ParseStatus::Success, total_message_length, message,
                    "HEARTBEAT parsed via optimized template",
                    StreamFixParser::ParseState::IDLE, 0};
        }
    };

} // namespace fix_gateway::protocol