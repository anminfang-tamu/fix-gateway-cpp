#include "utils/logger.h"
#include <thread>
#include <chrono>
#include <vector>

using namespace fix_gateway::utils;

int main()
{
    // Configure logger
    Logger &logger = Logger::getInstance();
    logger.setLogFile("fix_gateway.log");
    logger.setLogLevel(LogLevel::DEBUG);

    // Test different logging methods
    LOG_DEBUG("Debug message from main");
    LOG_INFO("FIX Gateway starting up...");
    LOG_WARN("This is a warning message");
    LOG_ERROR("This is an error message");

    // Test stream-like interface
    LOG(LogLevel::INFO) << "Order received: " << "Symbol=AAPL" << " Qty=" << 100;
    LOG_FLUSH();

    LOG(LogLevel::DEBUG) << "Connection established to " << "localhost:8080";
    LOG_FLUSH();

    // Test thread safety with multiple threads
    std::vector<std::thread> threads;

    for (int i = 0; i < 3; ++i)
    {
        threads.emplace_back([i]()
                             {
            for (int j = 0; j < 5; ++j) {
                LOG_INFO("Thread " + std::to_string(i) + " message " + std::to_string(j));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } });
    }

    // Wait for all threads to complete
    for (auto &t : threads)
    {
        t.join();
    }

    LOG_INFO("FIX Gateway shutdown complete");

    return 0;
}