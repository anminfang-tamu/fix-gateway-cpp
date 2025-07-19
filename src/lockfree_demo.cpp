#include "manager/message_manager.h"
#include "common/message.h"
#include "utils/performance_timer.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <random>

using namespace fix_gateway;

class LockFreeDemo
{
public:
    void run()
    {
        std::cout << "=== Phase 3: Lock-Free Queue Integration Demo ===" << std::endl;
        std::cout << std::endl;

        // Test 1: Basic functionality validation
        std::cout << "Test 1: Basic Functionality Validation" << std::endl;
        testBasicFunctionality();
        std::cout << std::endl;

        // Test 2: Performance comparison
        std::cout << "Test 2: Performance Comparison" << std::endl;
        performanceComparison();
        std::cout << std::endl;

        // Test 3: Load testing
        std::cout << "Test 3: Load Testing" << std::endl;
        loadTesting();
        std::cout << std::endl;

        std::cout << "=== Demo Complete ===" << std::endl;
    }

private:
    void testBasicFunctionality()
    {
        std::cout << "Testing mutex-based queues..." << std::endl;
        testQueueType(manager::MessageManagerFactory::createM1MaxConfig());

        std::cout << std::endl;
        std::cout << "Testing lock-free queues..." << std::endl;
        testQueueType(manager::MessageManagerFactory::createLockFreeM1MaxConfig());
    }

    void testQueueType(const manager::MessageManager::CorePinningConfig &config)
    {
        try
        {
            // Create message manager
            manager::MessageManager messageManager(config);

            std::cout << "  Queue Type: " << messageManager.getQueueTypeString() << std::endl;

            // Start the message manager
            messageManager.start();

            // Create test messages
            std::vector<common::MessagePtr> messages;
            for (int i = 0; i < 10; ++i)
            {
                Priority priority = static_cast<Priority>(i % 4);
                std::string id = "test_" + std::to_string(i);
                std::string payload = "Test message " + std::to_string(i);

                messages.push_back(common::Message::create(id, payload, priority));
            }

            // Route messages
            auto start_time = std::chrono::high_resolution_clock::now();

            int success_count = 0;
            for (const auto &message : messages)
            {
                if (messageManager.routeMessage(message))
                {
                    success_count++;
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

            std::cout << "  Messages routed: " << success_count << "/" << messages.size() << std::endl;
            std::cout << "  Routing time: " << duration.count() << " μs" << std::endl;
            std::cout << "  Avg per message: " << (duration.count() / messages.size()) << " μs" << std::endl;

            // Check queue depths
            auto depths = messageManager.getQueueDepths();
            std::cout << "  Queue depths: [";
            for (size_t i = 0; i < depths.size(); ++i)
            {
                std::cout << depths[i];
                if (i < depths.size() - 1)
                    std::cout << ", ";
            }
            std::cout << "]" << std::endl;

            // Wait a bit for processing
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Get statistics
            auto stats = messageManager.getStats();
            std::cout << "  Total messages sent: " << stats.total_messages_sent << std::endl;
            std::cout << "  Total messages failed: " << stats.total_messages_failed << std::endl;

            // Clean shutdown
            messageManager.stop();
        }
        catch (const std::exception &e)
        {
            std::cerr << "  ERROR: " << e.what() << std::endl;
        }
    }

    void performanceComparison()
    {
        const int NUM_MESSAGES = 1000;
        const int NUM_ITERATIONS = 5;

        std::cout << "Comparing performance with " << NUM_MESSAGES << " messages x " << NUM_ITERATIONS << " iterations:" << std::endl;

        // Test mutex-based performance
        auto mutex_config = manager::MessageManagerFactory::createM1MaxConfig();
        double mutex_avg_time = measurePerformance(mutex_config, NUM_MESSAGES, NUM_ITERATIONS);

        // Test lock-free performance
        auto lockfree_config = manager::MessageManagerFactory::createLockFreeM1MaxConfig();
        double lockfree_avg_time = measurePerformance(lockfree_config, NUM_MESSAGES, NUM_ITERATIONS);

        // Calculate improvement
        double improvement = ((mutex_avg_time - lockfree_avg_time) / mutex_avg_time) * 100.0;

        std::cout << std::endl;
        std::cout << "Performance Results:" << std::endl;
        std::cout << "  Mutex-based:  " << mutex_avg_time << " μs avg per message" << std::endl;
        std::cout << "  Lock-free:    " << lockfree_avg_time << " μs avg per message" << std::endl;
        std::cout << "  Improvement:  " << improvement << "%" << std::endl;
        std::cout << "  Speedup:      " << (mutex_avg_time / lockfree_avg_time) << "x" << std::endl;
    }

    double measurePerformance(const manager::MessageManager::CorePinningConfig &config,
                              int num_messages, int num_iterations)
    {
        double total_time = 0.0;

        for (int iter = 0; iter < num_iterations; ++iter)
        {
            try
            {
                manager::MessageManager messageManager(config);
                messageManager.start();

                // Create messages
                std::vector<common::MessagePtr> messages;
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(0, 3);

                for (int i = 0; i < num_messages; ++i)
                {
                    Priority priority = static_cast<Priority>(dis(gen));
                    std::string id = "perf_" + std::to_string(i);
                    std::string payload = "Performance test message " + std::to_string(i);

                    messages.push_back(common::Message::create(id, payload, priority));
                }

                // Measure routing time
                auto start_time = std::chrono::high_resolution_clock::now();

                for (const auto &message : messages)
                {
                    messageManager.routeMessage(message);
                }

                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

                double time_per_message = duration.count() / 1000.0 / num_messages; // Convert to microseconds
                total_time += time_per_message;

                messageManager.stop();
            }
            catch (const std::exception &e)
            {
                std::cerr << "  Performance test error: " << e.what() << std::endl;
            }
        }

        return total_time / num_iterations;
    }

    void loadTesting()
    {
        std::cout << "Running load test with lock-free queues..." << std::endl;

        const int NUM_THREADS = 4;
        const int MESSAGES_PER_THREAD = 1000;

        try
        {
            auto config = manager::MessageManagerFactory::createLockFreeM1MaxConfig();
            manager::MessageManager messageManager(config);
            messageManager.start();

            std::vector<std::thread> threads;
            std::atomic<int> total_success(0);

            auto start_time = std::chrono::high_resolution_clock::now();

            // Launch producer threads
            for (int t = 0; t < NUM_THREADS; ++t)
            {
                threads.emplace_back([&, t]()
                                     {
                    int local_success = 0;
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> dis(0, 3);
                    
                    for (int i = 0; i < MESSAGES_PER_THREAD; ++i)
                    {
                        Priority priority = static_cast<Priority>(dis(gen));
                        std::string id = "load_t" + std::to_string(t) + "_" + std::to_string(i);
                        std::string payload = "Load test message";
                        
                        auto message = common::Message::create(id, payload, priority);
                        if (messageManager.routeMessage(message))
                        {
                            local_success++;
                        }
                    }
                    
                    total_success += local_success; });
            }

            // Wait for all threads to complete
            for (auto &thread : threads)
            {
                thread.join();
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

            // Wait a bit for processing
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Get final statistics
            auto stats = messageManager.getStats();

            std::cout << "  Threads: " << NUM_THREADS << std::endl;
            std::cout << "  Total messages: " << (NUM_THREADS * MESSAGES_PER_THREAD) << std::endl;
            std::cout << "  Successfully routed: " << total_success.load() << std::endl;
            std::cout << "  Total time: " << duration.count() << " ms" << std::endl;
            std::cout << "  Messages/second: " << (total_success.load() * 1000.0 / duration.count()) << std::endl;
            std::cout << "  Messages sent: " << stats.total_messages_sent << std::endl;
            std::cout << "  Messages failed: " << stats.total_messages_failed << std::endl;

            auto depths = messageManager.getQueueDepths();
            std::cout << "  Final queue depths: [";
            for (size_t i = 0; i < depths.size(); ++i)
            {
                std::cout << depths[i];
                if (i < depths.size() - 1)
                    std::cout << ", ";
            }
            std::cout << "]" << std::endl;

            messageManager.stop();
        }
        catch (const std::exception &e)
        {
            std::cerr << "  Load test error: " << e.what() << std::endl;
        }
    }
};

int main()
{
    try
    {
        LockFreeDemo demo;
        demo.run();
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Demo failed: " << e.what() << std::endl;
        return 1;
    }
}