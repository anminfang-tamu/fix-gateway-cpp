#pragma once

#include <cstddef>
#include <chrono>

namespace fix_gateway
{
    namespace constants
    {

        // =============================================================================
        // NETWORK CONSTANTS
        // =============================================================================

        // Socket constants
        constexpr int INVALID_SOCKET = -1;
        constexpr size_t BUFFER_SIZE = 8192;        // 8KB default buffer
        constexpr size_t LARGE_BUFFER_SIZE = 65536; // 64KB for high-volume data
        constexpr size_t SMALL_BUFFER_SIZE = 1024;  // 1KB for control messages

        // Network timeouts (milliseconds)
        constexpr int CONNECTION_TIMEOUT_MS = 5000; // 5 seconds
        constexpr int RECV_TIMEOUT_MS = 1000;       // 1 second
        constexpr int SEND_TIMEOUT_MS = 1000;       // 1 second
        constexpr int RECONNECT_DELAY_MS = 1000;    // 1 second between reconnect attempts
        constexpr int MAX_RECONNECT_ATTEMPTS = 5;

        // =============================================================================
        // FIX PROTOCOL CONSTANTS
        // =============================================================================

        // FIX message delimiters
        constexpr char SOH = 0x01; // Start of Header delimiter
        constexpr const char *FIX_VERSION = "FIX.4.4";
        constexpr size_t MAX_FIX_MESSAGE_SIZE = 4096; // 4KB max FIX message
        constexpr size_t MIN_FIX_MESSAGE_SIZE = 20;   // Minimum valid FIX message

        // FIX session timeouts
        constexpr int DEFAULT_HEARTBEAT_INTERVAL = 30; // 30 seconds
        constexpr int MAX_HEARTBEAT_INTERVAL = 300;    // 5 minutes
        constexpr int MIN_HEARTBEAT_INTERVAL = 10;     // 10 seconds
        constexpr int LOGON_TIMEOUT_SECONDS = 30;      // 30 seconds to complete logon

        // FIX sequence numbers
        constexpr int INITIAL_SEQUENCE_NUMBER = 1;
        constexpr int MAX_SEQUENCE_NUMBER = 999999999;

        // =============================================================================
        // THREADING CONSTANTS
        // =============================================================================

        // Thread pool sizes
        constexpr size_t DEFAULT_THREAD_POOL_SIZE = 4;
        constexpr size_t MAX_THREAD_POOL_SIZE = 16;

        // Queue sizes
        constexpr size_t MESSAGE_QUEUE_SIZE = 1000;
        constexpr size_t ORDER_QUEUE_SIZE = 500;

        // =============================================================================
        // LOGGING CONSTANTS
        // =============================================================================

        // Log file settings
        constexpr size_t MAX_LOG_FILE_SIZE = 100 * 1024 * 1024; // 100MB
        constexpr int MAX_LOG_FILES = 10;                       // Keep 10 old files
        constexpr const char *DEFAULT_LOG_FILE = "fix_gateway.log";

        // =============================================================================
        // TRADING SYSTEM CONSTANTS
        // =============================================================================

        // Order limits
        constexpr double MAX_ORDER_QUANTITY = 1000000.0; // 1M shares/contracts
        constexpr double MIN_ORDER_QUANTITY = 1.0;
        constexpr double MAX_ORDER_PRICE = 999999.99;
        constexpr double MIN_ORDER_PRICE = 0.01;

        // Risk limits
        constexpr int MAX_ORDERS_PER_SECOND = 100;
        constexpr int MAX_DAILY_ORDERS = 10000;

        // =============================================================================
        // GRPC CONSTANTS (for Order Book Interface)
        // =============================================================================

        // gRPC timeouts
        constexpr int GRPC_TIMEOUT_MS = 1000;             // 1 second
        constexpr int GRPC_CONNECTION_TIMEOUT_MS = 5000;  // 5 seconds
        constexpr int GRPC_KEEPALIVE_INTERVAL_MS = 30000; // 30 seconds

        // =============================================================================
        // CONFIGURATION CONSTANTS
        // =============================================================================

        // Default configuration file paths
        constexpr const char *DEFAULT_CONFIG_FILE = "config/fix_gateway.conf";
        constexpr const char *DEFAULT_CONFIG_DIR = "config/";

        // Environment variables
        constexpr const char *ENV_CONFIG_FILE = "FIX_GATEWAY_CONFIG";
        constexpr const char *ENV_LOG_LEVEL = "FIX_GATEWAY_LOG_LEVEL";

    } // namespace constants
} // namespace fix_gateway