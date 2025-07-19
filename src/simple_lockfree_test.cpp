#include "manager/message_manager.h"
#include "common/message.h"
#include "utils/performance_timer.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <iomanip>

using namespace fix_gateway;

int main()
{
    std::cout << "=== Phase 3: Simple Lock-Free Queue Test ===" << std::endl;
    std::cout << std::endl;

    // Test 1: Basic functionality - mutex-based
    std::cout << "Test 1: Basic Mutex-Based Queue Test" << std::endl;
    {
        auto config = manager::MessageManagerFactory::createM1MaxConfig();
        manager::MessageManager messageManager(config);

        std::cout << "Queue Type: " << messageManager.getQueueTypeString() << std::endl;

        // Don't start the full message manager - just test queue creation
        try
        {
            messageManager.start();
            std::cout << "✅ MessageManager started successfully" << std::endl;

            // Create a few test messages
            std::vector<common::MessagePtr> messages;
            for (int i = 0; i < 5; ++i)
            {
                Priority priority = static_cast<Priority>(i % 4);
                std::string id = "test_" + std::to_string(i);
                std::string payload = "Test message " + std::to_string(i);
                messages.push_back(common::Message::create(id, payload, priority));
            }

            // Route messages
            int success_count = 0;
            for (const auto &message : messages)
            {
                if (messageManager.routeMessage(message))
                {
                    success_count++;
                }
            }

            std::cout << "Messages routed: " << success_count << "/" << messages.size() << std::endl;

            // Check queue depths
            auto depths = messageManager.getQueueDepths();
            std::cout << "Queue depths: [";
            for (size_t i = 0; i < depths.size(); ++i)
            {
                std::cout << depths[i];
                if (i < depths.size() - 1)
                    std::cout << ", ";
            }
            std::cout << "]" << std::endl;

            messageManager.stop();
            std::cout << "✅ MessageManager stopped successfully" << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "❌ Error: " << e.what() << std::endl;
        }
    }

    std::cout << std::endl;

    // Test 2: Basic functionality - lock-free
    std::cout << "Test 2: Basic Lock-Free Queue Test" << std::endl;
    {
        auto config = manager::MessageManagerFactory::createLockFreeM1MaxConfig();
        manager::MessageManager messageManager(config);

        std::cout << "Queue Type: " << messageManager.getQueueTypeString() << std::endl;

        try
        {
            messageManager.start();
            std::cout << "✅ MessageManager started successfully" << std::endl;

            // Create a few test messages
            std::vector<common::MessagePtr> messages;
            for (int i = 0; i < 5; ++i)
            {
                Priority priority = static_cast<Priority>(i % 4);
                std::string id = "test_" + std::to_string(i);
                std::string payload = "Test message " + std::to_string(i);
                messages.push_back(common::Message::create(id, payload, priority));
            }

            // Route messages
            int success_count = 0;
            for (const auto &message : messages)
            {
                if (messageManager.routeMessage(message))
                {
                    success_count++;
                }
            }

            std::cout << "Messages routed: " << success_count << "/" << messages.size() << std::endl;

            // Check queue depths
            auto depths = messageManager.getQueueDepths();
            std::cout << "Queue depths: [";
            for (size_t i = 0; i < depths.size(); ++i)
            {
                std::cout << depths[i];
                if (i < depths.size() - 1)
                    std::cout << ", ";
            }
            std::cout << "]" << std::endl;

            messageManager.stop();
            std::cout << "✅ MessageManager stopped successfully" << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "❌ Error: " << e.what() << std::endl;
        }
    }

    std::cout << std::endl;

    // Test 3: Performance comparison
    std::cout << "Test 3: Performance Comparison (Single-threaded routing)" << std::endl;

    const int NUM_MESSAGES = 1000;
    const int NUM_ITERATIONS = 3;

    auto test_performance = [](const manager::MessageManager::CorePinningConfig &config,
                               const std::string &name) -> double
    {
        double total_time = 0.0;

        for (int iter = 0; iter < NUM_ITERATIONS; ++iter)
        {
            manager::MessageManager messageManager(config);
            messageManager.start();

            // Create messages
            std::vector<common::MessagePtr> messages;
            for (int i = 0; i < NUM_MESSAGES; ++i)
            {
                Priority priority = static_cast<Priority>(i % 4);
                std::string id = "perf_" + std::to_string(i);
                std::string payload = "Performance test message " + std::to_string(i);
                messages.push_back(common::Message::create(id, payload, priority));
            }

            // Measure routing time
            auto start = std::chrono::high_resolution_clock::now();

            int success_count = 0;
            for (const auto &message : messages)
            {
                if (messageManager.routeMessage(message))
                {
                    success_count++;
                }
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

            double time_per_message = duration.count() / 1000.0 / NUM_MESSAGES; // microseconds
            total_time += time_per_message;

            std::cout << "  " << name << " iter " << (iter + 1) << ": "
                      << success_count << "/" << NUM_MESSAGES << " routed, "
                      << std::fixed << std::setprecision(3) << time_per_message << " μs/msg" << std::endl;

            messageManager.stop();
        }

        return total_time / NUM_ITERATIONS;
    };

    // Test both configurations
    auto mutex_config = manager::MessageManagerFactory::createM1MaxConfig();
    auto lockfree_config = manager::MessageManagerFactory::createLockFreeM1MaxConfig();

    std::cout << "\nMutex-based performance:" << std::endl;
    double mutex_avg = test_performance(mutex_config, "Mutex");

    std::cout << "\nLock-free performance:" << std::endl;
    double lockfree_avg = test_performance(lockfree_config, "Lock-free");

    std::cout << "\n=== Performance Summary ===" << std::endl;
    std::cout << "Mutex-based average:  " << std::fixed << std::setprecision(3) << mutex_avg << " μs/msg" << std::endl;
    std::cout << "Lock-free average:    " << std::fixed << std::setprecision(3) << lockfree_avg << " μs/msg" << std::endl;

    if (lockfree_avg > 0)
    {
        double speedup = mutex_avg / lockfree_avg;
        double improvement = ((mutex_avg - lockfree_avg) / mutex_avg) * 100.0;
        std::cout << "Speedup:              " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;
        std::cout << "Improvement:          " << std::fixed << std::setprecision(1) << improvement << "%" << std::endl;
    }

    std::cout << "\n=== Test Complete ===" << std::endl;

    return 0;
}