#pragma once

#include "fix_message.h"
#include "fix_fields.h"
#include "common/message_pool.h"
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

    // Zero-copy FIX parser optimized for high-frequency trading
    // Enhanced with state machine for production-grade partial packet handling
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
            Success,              // Message parsed successfully
            NeedMoreData,         // Incomplete message, need more bytes
            InvalidFormat,        // Malformed FIX message
            ChecksumError,        // Checksum validation failed
            AllocationFailed,     // MessagePool allocation failed
            MessageTooLarge,      // Message exceeds size limits
            UnsupportedVersion,   // FIX version not supported
            StateTransitionError, // Invalid state machine transition
            FieldParseError,      // Error parsing specific field
            RecoverySuccess,      // Successfully recovered from error
            CorruptedData         // Data corruption detected
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

    protected:
        // =================================================================
        // CORE PARSING FUNCTIONS (For Testing)
        // =================================================================

        // Expose core parsing functions for testing
        ParseResult findCompleteMessage(const char *buffer, size_t length, size_t &message_start, size_t &message_end);
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
        ParseResult combineBuffersAndParse(const char *new_buffer, size_t new_length);

        // Buffer management with state preservation
        void storePartialMessage(const char *buffer, size_t length);
        void appendToPartialBuffer(const char *buffer, size_t length);
        bool hasSpaceInPartialBuffer(size_t additional_bytes) const;

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
        static constexpr size_t PARTIAL_BUFFER_SIZE = 16384; // 16KB buffer
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

} // namespace fix_gateway::protocol