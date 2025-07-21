#pragma once

#include "fix_message.h"
#include "fix_fields.h"
#include <string>
#include <string_view>
#include <memory>
#include <vector>

namespace fix_gateway::protocol
{

    class FixParser
    {
    public:
        // Parsing result codes
        enum class ParseResult
        {
            Success,
            IncompleteMessage,
            InvalidFormat,
            ChecksumError,
            MissingRequiredField,
            InvalidFieldFormat,
            MessageTooLarge,
            UnsupportedVersion
        };

        // Parser statistics for performance monitoring
        struct ParserStats
        {
            uint64_t messagesParseAttempts = 0;
            uint64_t messagesParseSuccess = 0;
            uint64_t messagesParseFailure = 0;
            uint64_t totalParseTimeNanos = 0;
            uint64_t averageParseTimeNanos = 0;
            uint64_t lastParseTimeNanos = 0;

            void updateTiming(uint64_t parseTimeNanos);
            void reset();
        };

        // Constructor
        FixParser();

        // Disable copy (but allow move)
        FixParser(const FixParser &) = delete;
        FixParser &operator=(const FixParser &) = delete;
        FixParser(FixParser &&) = default;
        FixParser &operator=(FixParser &&) = default;

        // Core parsing methods
        ParseResult parse(const std::string &rawMessage, FixMessagePtr &message);
        ParseResult parse(const char *data, size_t length, FixMessagePtr &message);
        ParseResult parse(std::string_view messageView, FixMessagePtr &message);

        // Streaming parser for incomplete messages
        ParseResult parseStreaming(const char *data, size_t length,
                                   std::vector<FixMessagePtr> &messages);
        void resetStreamingState();
        size_t getStreamingBufferSize() const { return streamBuffer_.size(); }

        // Validation-only parsing (faster when you don't need the message object)
        ParseResult validateMessage(const std::string &rawMessage);
        ParseResult validateMessage(const char *data, size_t length);

        // Fast message type extraction (without full parsing)
        static std::string extractMsgType(const std::string &rawMessage);
        static std::string extractMsgType(const char *data, size_t length);

        // Fast field extraction (without full parsing)
        static bool extractField(const std::string &rawMessage, int fieldTag, std::string &value);
        static bool extractField(const char *data, size_t length, int fieldTag, std::string &value);

        // Configuration
        void setMaxMessageSize(size_t maxSize) { maxMessageSize_ = maxSize; }
        void setStrictValidation(bool strict) { strictValidation_ = strict; }
        void setValidateChecksum(bool validate) { validateChecksum_ = validate; }
        void setRequireBeginString(bool require) { requireBeginString_ = require; }

        // Statistics and monitoring
        const ParserStats &getStats() const { return stats_; }
        void resetStats() { stats_.reset(); }

        // Error information for last parse attempt
        std::string getLastError() const { return lastError_; }
        size_t getLastErrorPosition() const { return lastErrorPosition_; }

        // Utility methods
        static bool isCompleteMessage(const std::string &data);
        static bool isCompleteMessage(const char *data, size_t length);
        static size_t findMessageEnd(const char *data, size_t length);
        static size_t countFields(const std::string &rawMessage);

    private:
        // Parser configuration
        size_t maxMessageSize_ = 8192;   // 8KB default max message size
        bool strictValidation_ = true;   // Strict FIX validation
        bool validateChecksum_ = true;   // Validate message checksum
        bool requireBeginString_ = true; // Require BeginString field

        // Streaming parser state
        std::string streamBuffer_;

        // Performance tracking
        ParserStats stats_;

        // Error tracking
        std::string lastError_;
        size_t lastErrorPosition_ = 0;

        // Core parsing implementation
        ParseResult parseImpl(const char *data, size_t length, FixMessagePtr &message);

        // Parsing helpers
        ParseResult extractFields(const char *data, size_t length,
                                  FixMessage::FieldMap &fields);
        bool parseField(const char *&current, const char *end,
                        int &tag, std::string &value);

        // Validation helpers
        ParseResult validateBeginString(const FixMessage::FieldMap &fields);
        ParseResult validateBodyLength(const char *data, size_t length,
                                       const FixMessage::FieldMap &fields);
        ParseResult validateChecksum(const char *data, size_t length,
                                     const FixMessage::FieldMap &fields);
        ParseResult validateRequiredFields(const FixMessage::FieldMap &fields);
        ParseResult validateFieldFormats(const FixMessage::FieldMap &fields);

        // Utility methods
        void setError(const std::string &error, size_t position = 0);
        void clearError();

        // Performance helpers
        void startTiming();
        void endTiming();
        std::chrono::steady_clock::time_point parseStartTime_;
    };

    // Standalone utility functions for FIX parsing
    namespace FixParserUtils
    {
        // Message boundary detection
        std::vector<std::string_view> splitMessages(std::string_view data);
        size_t findNextMessage(const char *data, size_t length, size_t startPos = 0);

        // Field parsing utilities
        bool parseTagValue(std::string_view field, int &tag, std::string &value);
        std::string_view extractFieldValue(std::string_view message, int fieldTag);

        // Checksum utilities
        uint8_t calculateChecksum(const char *data, size_t length);
        bool verifyChecksum(const char *data, size_t length);
        std::string formatChecksum(uint8_t checksum);

        // Message length utilities
        size_t calculateBodyLength(const char *data, size_t totalLength);
        bool verifyBodyLength(const char *data, size_t length);

        // Format validation
        bool isValidFieldTag(int tag);
        bool isValidFieldValue(int tag, const std::string &value);
        bool hasValidFormat(std::string_view message);

        // Performance profiling
        struct ParseProfile
        {
            std::chrono::nanoseconds fieldExtractionTime{0};
            std::chrono::nanoseconds validationTime{0};
            std::chrono::nanoseconds objectCreationTime{0};
            size_t fieldCount = 0;
            size_t messageLength = 0;
        };

        ParseProfile profileParsing(const std::string &message);

        // Error descriptions
        std::string getParseResultDescription(FixParser::ParseResult result);
    }

} // namespace fix_gateway::protocol