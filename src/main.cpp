#include "utils/logger.h"
#include "network/tcp_connection.h"
#include <iostream>
#include <thread>
#include <chrono>

int main()
{
    // Initialize logger
    LOG_INFO("=== FIX Gateway TCP Connection Test ===");

    // Create TCP connection
    fix_gateway::network::TcpConnection connection;

    // Set up callbacks
    connection.setDataCallback([](const std::vector<char> &data)
                               {
        std::string message(data.begin(), data.end());
        LOG_INFO("Received data: " + message); });

    connection.setErrorCallback([](const std::string &error)
                                { LOG_ERROR("Connection error: " + error); });

    connection.setDisconnectCallback([]()
                                     { LOG_WARN("Connection disconnected!"); });

    LOG_INFO("TCP connection created with callbacks configured");

    // Test connection attempt (this will fail since there's no server)
    LOG_INFO("Attempting to connect to localhost:8080 (expected to fail)");

    try
    {
        bool connected = connection.connect("localhost", 8080);
        if (connected)
        {
            LOG_INFO("Connected successfully!");

            // Start receive loop
            connection.startReceiveLoop();

            // Send a test message
            connection.send("Hello FIX Server!");

            // Keep alive for a bit
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // Disconnect
            connection.disconnect();
        }
        else
        {
            LOG_INFO("Connection failed as expected (no server running)");
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception during connection: " + std::string(e.what()));
    }

    LOG_INFO("=== TCP Connection Test Completed ===");

    return 0;
}