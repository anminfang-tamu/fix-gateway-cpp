#pragma once

#include <string>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

namespace fix_gateway
{
    namespace utils
    {

        enum class LogLevel
        {
            DEBUG = 0,
            INFO = 1,
            WARN = 2,
            ERROR = 3,
            FATAL = 4
        };

        class Logger
        {
        public:
            static Logger &getInstance();

            // Configuration
            void setLogLevel(LogLevel level);
            void setLogFile(const std::string &filename);
            void enableConsoleOutput(bool enable);
            void enableTimestamp(bool enable);

            // Logging methods
            void log(LogLevel level, const std::string &message);
            void debug(const std::string &message);
            void info(const std::string &message);
            void warn(const std::string &message);
            void error(const std::string &message);
            void fatal(const std::string &message);

            // Convenient stream-like logging
            template <typename T>
            Logger &operator<<(const T &value)
            {
                stream_ << value;
                return *this;
            }

            // Special handling for LogLevel to start a new log entry
            Logger &operator<<(LogLevel level);

            // Flush the current stream content
            void flush();

        private:
            Logger() = default;
            ~Logger();

            // Non-copyable, non-movable
            Logger(const Logger &) = delete;
            Logger &operator=(const Logger &) = delete;
            Logger(Logger &&) = delete;
            Logger &operator=(Logger &&) = delete;

            std::string formatMessage(LogLevel level, const std::string &message);
            std::string getCurrentTimestamp();
            std::string levelToString(LogLevel level);

            LogLevel current_level_ = LogLevel::INFO;
            bool console_output_ = true;
            bool timestamp_enabled_ = true;

            std::unique_ptr<std::ofstream> file_stream_;
            std::mutex mutex_;

            // For stream-like interface
            std::ostringstream stream_;
            LogLevel pending_level_ = LogLevel::INFO;
        };

// Convenience macros for easy logging
#define LOG_DEBUG(msg) fix_gateway::utils::Logger::getInstance().debug(msg)
#define LOG_INFO(msg) fix_gateway::utils::Logger::getInstance().info(msg)
#define LOG_WARN(msg) fix_gateway::utils::Logger::getInstance().warn(msg)
#define LOG_ERROR(msg) fix_gateway::utils::Logger::getInstance().error(msg)
#define LOG_FATAL(msg) fix_gateway::utils::Logger::getInstance().fatal(msg)

// Stream-like logging macros
#define LOG(level) fix_gateway::utils::Logger::getInstance() << level
#define LOG_FLUSH() fix_gateway::utils::Logger::getInstance().flush()

    } // namespace utils
} // namespace fix_gateway