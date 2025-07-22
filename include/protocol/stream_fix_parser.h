#pragma once

#include "fix_message.h"
#include "fix_fields.h"
#include "common/message_pool.h"
#include <string>
#include <string_view>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace fix_gateway::protocol
{
    using namespace fix_gateway::common;

    // Zero-copy FIX parser optimized for high-frequency trading
    // Integrates directly with MessagePool<FixMessage> for sub-100ns parsing
    class StreamFixParser
    {
    public:
        // =================================================================
        // Parse result status
        enum class ParseStatus
        {
            Success,           // Message parsed successfully
            NeedMoreData,      // Incomplete message, need more bytes
            InvalidFormat,     // Malformed FIX message
            ChecksumError,     // Checksum validation failed
            AllocationFailed,  // MessagePool allocation failed
            MessageTooLarge,   // Message exceeds size limits
            UnsupportedVersion // FIX version not supported
        };

        // Parse result containing status and parsed message
        struct ParseResult
        {
            ParseStatus status;
            size_t bytes_consumed;      // How many bytes were processed
            FixMessage *parsed_message; // Raw pointer from pool (nullptr on failure)
            std::string error_detail;   // Error description for debugging
        };

        // Parser statistics for performance monitoring
        struct ParserStats
        {
            uint64_t messages_parsed = 0;
            uint64_t parse_errors = 0;
            uint64_t checksum_errors = 0;
            uint64_t allocation_failures = 0;
            uint64_t total_parse_time_ns = 0;
            uint64_t max_parse_time_ns = 0;
            uint64_t min_parse_time_ns = UINT64_MAX;

            double getAverageParseTimeNs() const
            {
                return messages_parsed > 0 ? static_cast<double>(total_parse_time_ns) / messages_parsed : 0.0;
            }

            void reset()
            {
                messages_parsed = parse_errors = checksum_errors = allocation_failures = 0;
                total_parse_time_ns = max_parse_time_ns = 0;
                min_parse_time_ns = UINT64_MAX;
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
        // CORE PARSING METHODS (Zero-Copy Interface)
        // =================================================================

        // Parse from raw network buffer - MAIN ENTRY POINT
        ParseResult parse(const char *buffer, size_t length);

        // Parse multiple messages from buffer (streaming)
        std::vector<ParseResult> parseStream(const char *buffer, size_t length);

        // =================================================================
        // STREAMING STATE MANAGEMENT (For TCP packet fragmentation)
        // =================================================================

        // Check if parser has partial message from previous parse
        bool hasPartialMessage() const { return partial_buffer_size_ > 0; }

        // Get size of partial message buffer
        size_t getPartialMessageSize() const { return partial_buffer_size_; }

        // Reset parser state (clear partial messages)
        void reset();

        // =================================================================
        // CONFIGURATION
        // =================================================================

        // Set maximum message size (default: 8KB)
        void setMaxMessageSize(size_t max_size) { max_message_size_ = max_size; }

        // Enable/disable checksum validation (default: enabled)
        void setValidateChecksum(bool validate) { validate_checksum_ = validate; }

        // Enable/disable strict FIX validation (default: enabled)
        void setStrictValidation(bool strict) { strict_validation_ = strict; }

        // =================================================================
        // PERFORMANCE MONITORING
        // =================================================================

        // Get parser statistics
        const ParserStats &getStats() const { return stats_; }

        // Reset statistics
        void resetStats() { stats_.reset(); }

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

    private:
        // =================================================================
        // CORE PARSING IMPLEMENTATION
        // =================================================================

        // Find complete message in buffer
        ParseResult findCompleteMessage(const char *buffer, size_t length,
                                        size_t &message_start, size_t &message_end);

        // Parse single message from buffer range
        ParseResult parseMessage(const char *buffer, size_t start_pos, size_t end_pos);

        // Extract fields from message body (zero-copy)
        bool extractFields(FixMessage *msg, const char *body_start, size_t body_length);

        // =================================================================
        // FIX PROTOCOL HELPERS
        // =================================================================

        // Find field in buffer: returns pointer to value start and length
        bool findField(const char *buffer, size_t length, int field_tag,
                       const char *&value_start, size_t &value_length);

        // Parse integer from buffer (ASCII to int)
        bool parseInteger(const char *buffer, size_t length, int &result);

        // Calculate checksum for message body
        uint8_t calculateChecksum(const char *buffer, size_t length);

        // Validate FIX message structure
        bool validateMessageStructure(const char *buffer, size_t length);

        // Validate parsed message has required fields
        bool validateParsedMessage(FixMessage *message);

        // Validate message checksum
        bool validateMessageChecksum(const char *buffer, size_t length);

        // Update parser statistics
        void updateStats(ParseStatus status, uint64_t parse_time_ns);

        // =================================================================
        // PARTIAL MESSAGE HANDLING (TCP fragmentation)
        // =================================================================

        // Combine partial buffer with new data
        ParseResult handlePartialMessage(const char *new_buffer, size_t new_length);

        // Store partial message for next parse call
        void storePartialMessage(const char *buffer, size_t length);

        // =================================================================
        // MEMBER VARIABLES
        // =================================================================

        // Message pool for allocation
        MessagePool<FixMessage> *message_pool_;

        // Configuration
        size_t max_message_size_;
        bool validate_checksum_;
        bool strict_validation_;

        // Partial message handling (TCP fragmentation)
        static constexpr size_t PARTIAL_BUFFER_SIZE = 16384; // 16KB buffer
        char partial_buffer_[PARTIAL_BUFFER_SIZE];
        size_t partial_buffer_size_;

        // Performance statistics
        mutable ParserStats stats_;

        // Timing for performance measurement
        std::chrono::high_resolution_clock::time_point parse_start_time_;
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