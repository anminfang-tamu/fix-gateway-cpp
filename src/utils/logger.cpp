#include "utils/logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>

namespace fix_gateway
{
    namespace utils
    {

        Logger &Logger::getInstance()
        {
            static Logger instance;
            return instance;
        }

        Logger::~Logger()
        {
            if (file_stream_ && file_stream_->is_open())
            {
                file_stream_->close();
            }
        }

        void Logger::setLogLevel(LogLevel level)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_level_ = level;
        }

        void Logger::setLogFile(const std::string &filename)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (file_stream_ && file_stream_->is_open())
            {
                file_stream_->close();
            }

            file_stream_ = std::make_unique<std::ofstream>(filename, std::ios::app);
            if (!file_stream_->is_open())
            {
                std::cerr << "Failed to open log file: " << filename << std::endl;
            }
        }

        void Logger::enableConsoleOutput(bool enable)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            console_output_ = enable;
        }

        void Logger::enableTimestamp(bool enable)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            timestamp_enabled_ = enable;
        }

        void Logger::log(LogLevel level, const std::string &message)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            // Check if this log level should be output
            if (level < current_level_)
            {
                return;
            }

            std::string formatted_message = formatMessage(level, message);

            // Output to console
            if (console_output_)
            {
                if (level >= LogLevel::ERROR)
                {
                    std::cerr << formatted_message << std::endl;
                }
                else
                {
                    std::cout << formatted_message << std::endl;
                }
            }

            // Output to file
            if (file_stream_ && file_stream_->is_open())
            {
                *file_stream_ << formatted_message << std::endl;
                file_stream_->flush(); // Ensure immediate write for trading systems
            }
        }

        void Logger::debug(const std::string &message)
        {
            log(LogLevel::DEBUG, message);
        }

        void Logger::info(const std::string &message)
        {
            log(LogLevel::INFO, message);
        }

        void Logger::warn(const std::string &message)
        {
            log(LogLevel::WARN, message);
        }

        void Logger::error(const std::string &message)
        {
            log(LogLevel::ERROR, message);
        }

        void Logger::fatal(const std::string &message)
        {
            log(LogLevel::FATAL, message);
        }

        Logger &Logger::operator<<(LogLevel level)
        {
            pending_level_ = level;
            return *this;
        }

        void Logger::flush()
        {
            std::string content = stream_.str();
            if (!content.empty())
            {
                log(pending_level_, content);
                stream_.str(""); // Clear the stream
                stream_.clear(); // Clear any error flags
            }
        }

        std::string Logger::formatMessage(LogLevel level, const std::string &message)
        {
            std::ostringstream oss;

            if (timestamp_enabled_)
            {
                oss << "[" << getCurrentTimestamp() << "] ";
            }

            oss << "[" << levelToString(level) << "] " << message;

            return oss.str();
        }

        std::string Logger::getCurrentTimestamp()
        {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch()) %
                      1000;

            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            oss << "." << std::setfill('0') << std::setw(3) << ms.count();

            return oss.str();
        }

        std::string Logger::levelToString(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::DEBUG:
                return "DEBUG";
            case LogLevel::INFO:
                return "INFO ";
            case LogLevel::WARN:
                return "WARN ";
            case LogLevel::ERROR:
                return "ERROR";
            case LogLevel::FATAL:
                return "FATAL";
            default:
                return "UNKNOWN";
            }
        }

    } // namespace utils
} // namespace fix_gateway