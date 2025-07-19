#include "common/message_pool.h"
#include "common/message.h"
#include "utils/performance_timer.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <random>

using namespace fix_gateway::common;
using namespace fix_gateway::utils;

// Test configuration
constexpr size_t POOL_SIZE = 1000;
constexpr size_t NUM_MESSAGES = 10000;
constexpr size_t NUM_THREADS = 4;

// Performance test results
struct TestResults
{
    double allocation_time_ns;
    double deallocation_time_ns;
    double total_time_ns;
    size_t successful_allocations;
    size_t failed_allocations;
    std::string test_name;
};

// Test 1: Basic pool functionality
void testBasicPoolFunctionality()
{
    std::cout << "\n=== Test 1: Basic Pool Functionality ===\n";

    MessagePool pool(100, "test_pool");

    // Test allocation
    auto msg1 = pool.allocate("test_1", "Hello World", Priority::HIGH, MessageType::ORDER, "session1", "dest1");
    auto msg2 = pool.allocate("test_2", "Hello World", Priority::CRITICAL, MessageType::CANCEL, "session2", "dest2");

    if (msg1 && msg2)
    {
        std::cout << "✅ Message allocation: SUCCESS\n";
        std::cout << "   Message 1: " << msg1->toString() << "\n";
        std::cout << "   Message 2: " << msg2->toString() << "\n";
    }
    else
    {
        std::cout << "❌ Message allocation: FAILED\n";
    }

    // Test pool statistics
    auto stats = pool.getStats();
    std::cout << "📊 Pool Statistics:\n";
    std::cout << "   Capacity: " << stats.total_capacity << "\n";
    std::cout << "   Allocated: " << stats.allocated_count << "\n";
    std::cout << "   Available: " << stats.available_count << "\n";
    std::cout << "   Utilization: " << (stats.allocated_count * 100.0 / stats.total_capacity) << "%\n";

    // Test deallocation (automatic with shared_ptr)
    msg1.reset();
    msg2.reset();

    // Check stats after deallocation
    stats = pool.getStats();
    std::cout << "📊 Pool Statistics (after deallocation):\n";
    std::cout << "   Allocated: " << stats.allocated_count << "\n";
    std::cout << "   Available: " << stats.available_count << "\n";
    std::cout << "   Total Allocations: " << stats.total_allocations << "\n";
    std::cout << "   Total Deallocations: " << stats.total_deallocations << "\n";
}

// Test 2: Pool exhaustion behavior
void testPoolExhaustion()
{
    std::cout << "\n=== Test 2: Pool Exhaustion Behavior ===\n";

    MessagePool pool(5, "small_pool"); // Small pool for testing
    std::vector<PooledMessagePtr> messages;

    // Allocate until pool is exhausted
    for (size_t i = 0; i < 10; ++i)
    {
        auto msg = pool.allocate("test_" + std::to_string(i), "payload", Priority::LOW);
        if (msg)
        {
            messages.push_back(msg);
            std::cout << "✅ Allocated message " << i << " (pool usage: " << pool.allocated() << "/5)\n";
        }
        else
        {
            std::cout << "❌ Failed to allocate message " << i << " (pool exhausted)\n";
        }
    }

    // Test pool statistics
    auto stats = pool.getStats();
    std::cout << "📊 Final Pool Statistics:\n";
    std::cout << "   Capacity: " << stats.total_capacity << "\n";
    std::cout << "   Allocated: " << stats.allocated_count << "\n";
    std::cout << "   Available: " << stats.available_count << "\n";
    std::cout << "   Allocation Failures: " << stats.allocation_failures << "\n";
}

// Test 3: Global pool functionality
void testGlobalPool()
{
    std::cout << "\n=== Test 3: Global Pool Functionality ===\n";

    // Test global pool allocation
    auto msg1 = GlobalMessagePool::allocate("global_1", "Global Message 1", Priority::HIGH);
    auto msg2 = pool::createMessage("global_2", "Global Message 2", Priority::CRITICAL);

    if (msg1 && msg2)
    {
        std::cout << "✅ Global pool allocation: SUCCESS\n";
        std::cout << "   Message 1: " << msg1->getMessageId() << ", Priority: " << msg1->getPriorityString() << "\n";
        std::cout << "   Message 2: " << msg2->getMessageId() << ", Priority: " << msg2->getPriorityString() << "\n";
    }
    else
    {
        std::cout << "❌ Global pool allocation: FAILED\n";
    }

    // Test global pool statistics
    auto &global_pool = GlobalMessagePool::getInstance();
    auto stats = global_pool.getStats();
    std::cout << "📊 Global Pool Statistics:\n";
    std::cout << "   Capacity: " << stats.total_capacity << "\n";
    std::cout << "   Allocated: " << stats.allocated_count << "\n";
    std::cout << "   Available: " << stats.available_count << "\n";
}

// Test 4: Performance comparison - Pool vs Dynamic allocation
TestResults performanceTestDynamic(size_t num_messages)
{
    TestResults results;
    results.test_name = "Dynamic Allocation";
    results.successful_allocations = 0;
    results.failed_allocations = 0;

    std::vector<MessagePtr> messages;
    messages.reserve(num_messages);

    // Measure allocation time
    auto start_alloc = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_messages; ++i)
    {
        try
        {
            auto msg = Message::create("dynamic_" + std::to_string(i),
                                       "Dynamic payload " + std::to_string(i),
                                       Priority::LOW, MessageType::ORDER);
            messages.push_back(msg);
            results.successful_allocations++;
        }
        catch (...)
        {
            results.failed_allocations++;
        }
    }

    auto end_alloc = std::chrono::high_resolution_clock::now();
    results.allocation_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_alloc - start_alloc).count();

    // Measure deallocation time
    auto start_dealloc = std::chrono::high_resolution_clock::now();

    messages.clear();

    auto end_dealloc = std::chrono::high_resolution_clock::now();
    results.deallocation_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_dealloc - start_dealloc).count();

    results.total_time_ns = results.allocation_time_ns + results.deallocation_time_ns;

    return results;
}

TestResults performanceTestPool(size_t num_messages)
{
    TestResults results;
    results.test_name = "Pool Allocation";
    results.successful_allocations = 0;
    results.failed_allocations = 0;

    MessagePool pool(num_messages + 100, "perf_test_pool"); // Slightly larger than needed
    pool.prewarm();

    std::vector<PooledMessagePtr> messages;
    messages.reserve(num_messages);

    // Measure allocation time
    auto start_alloc = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_messages; ++i)
    {
        auto msg = pool.allocate("pool_" + std::to_string(i),
                                 "Pool payload " + std::to_string(i),
                                 Priority::LOW, MessageType::ORDER);
        if (msg)
        {
            messages.push_back(msg);
            results.successful_allocations++;
        }
        else
        {
            results.failed_allocations++;
        }
    }

    auto end_alloc = std::chrono::high_resolution_clock::now();
    results.allocation_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_alloc - start_alloc).count();

    // Measure deallocation time
    auto start_dealloc = std::chrono::high_resolution_clock::now();

    messages.clear();

    auto end_dealloc = std::chrono::high_resolution_clock::now();
    results.deallocation_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_dealloc - start_dealloc).count();

    results.total_time_ns = results.allocation_time_ns + results.deallocation_time_ns;

    return results;
}

void runPerformanceComparison()
{
    std::cout << "\n=== Test 4: Performance Comparison ===\n";

    const size_t test_sizes[] = {1000, 5000, 10000};

    for (size_t test_size : test_sizes)
    {
        std::cout << "\n📊 Testing with " << test_size << " messages:\n";

        // Test dynamic allocation
        auto dynamic_results = performanceTestDynamic(test_size);

        // Test pool allocation
        auto pool_results = performanceTestPool(test_size);

        // Print results
        std::cout << "Dynamic Allocation:\n";
        std::cout << "  Total Time: " << (dynamic_results.total_time_ns / 1e6) << " ms\n";
        std::cout << "  Allocation Time: " << (dynamic_results.allocation_time_ns / 1e6) << " ms\n";
        std::cout << "  Deallocation Time: " << (dynamic_results.deallocation_time_ns / 1e6) << " ms\n";
        std::cout << "  Avg per message: " << (dynamic_results.total_time_ns / test_size) << " ns\n";
        std::cout << "  Successful: " << dynamic_results.successful_allocations << "\n";

        std::cout << "\nPool Allocation:\n";
        std::cout << "  Total Time: " << (pool_results.total_time_ns / 1e6) << " ms\n";
        std::cout << "  Allocation Time: " << (pool_results.allocation_time_ns / 1e6) << " ms\n";
        std::cout << "  Deallocation Time: " << (pool_results.deallocation_time_ns / 1e6) << " ms\n";
        std::cout << "  Avg per message: " << (pool_results.total_time_ns / test_size) << " ns\n";
        std::cout << "  Successful: " << pool_results.successful_allocations << "\n";

        // Calculate speedup
        double speedup = static_cast<double>(dynamic_results.total_time_ns) / pool_results.total_time_ns;
        double improvement = ((dynamic_results.total_time_ns - pool_results.total_time_ns) * 100.0) / dynamic_results.total_time_ns;

        std::cout << "\n🚀 Performance Improvement:\n";
        std::cout << "  Speedup: " << speedup << "x\n";
        std::cout << "  Improvement: " << improvement << "%\n";
        std::cout << "  Time Saved: " << ((dynamic_results.total_time_ns - pool_results.total_time_ns) / 1e6) << " ms\n";
    }
}

// Test 5: Multi-threaded allocation test
void threadedAllocationTest(MessagePool &pool, size_t thread_id, size_t num_allocations,
                            std::atomic<size_t> &total_success, std::atomic<size_t> &total_failures)
{
    size_t local_success = 0;
    size_t local_failures = 0;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 3);

    for (size_t i = 0; i < num_allocations; ++i)
    {
        std::string msg_id = "thread_" + std::to_string(thread_id) + "_msg_" + std::to_string(i);
        Priority priority = static_cast<Priority>(dis(gen));

        auto msg = pool.allocate(msg_id, "Multi-threaded payload", priority, MessageType::ORDER);

        if (msg)
        {
            local_success++;
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
        else
        {
            local_failures++;
        }
    }

    total_success.fetch_add(local_success, std::memory_order_relaxed);
    total_failures.fetch_add(local_failures, std::memory_order_relaxed);
}

void runMultiThreadedTest()
{
    std::cout << "\n=== Test 5: Multi-threaded Allocation Test ===\n";

    MessagePool pool(POOL_SIZE, "multithread_pool");
    pool.prewarm();

    std::atomic<size_t> total_success{0};
    std::atomic<size_t> total_failures{0};

    std::vector<std::thread> threads;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Launch threads
    for (size_t i = 0; i < NUM_THREADS; ++i)
    {
        threads.emplace_back(threadedAllocationTest, std::ref(pool), i,
                             NUM_MESSAGES / NUM_THREADS, std::ref(total_success), std::ref(total_failures));
    }

    // Wait for all threads to complete
    for (auto &thread : threads)
    {
        thread.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Print results
    std::cout << "🧵 Multi-threaded Test Results:\n";
    std::cout << "  Threads: " << NUM_THREADS << "\n";
    std::cout << "  Messages per thread: " << (NUM_MESSAGES / NUM_THREADS) << "\n";
    std::cout << "  Total messages: " << NUM_MESSAGES << "\n";
    std::cout << "  Successful allocations: " << total_success.load() << "\n";
    std::cout << "  Failed allocations: " << total_failures.load() << "\n";
    std::cout << "  Success rate: " << (total_success.load() * 100.0 / NUM_MESSAGES) << "%\n";
    std::cout << "  Duration: " << duration.count() << " ms\n";
    std::cout << "  Throughput: " << (total_success.load() * 1000.0 / duration.count()) << " allocations/sec\n";

    // Pool statistics
    auto stats = pool.getStats();
    std::cout << "\n📊 Final Pool Statistics:\n";
    std::cout << "  " << pool.toString() << "\n";
}

int main()
{
    std::cout << "=== Message Pool Test Suite ===\n";
    std::cout << "Testing lock-free message pool for trading systems\n";
    std::cout << "Target: Eliminate dynamic allocation for sub-10μs latency\n";

    try
    {
        // Run all tests
        testBasicPoolFunctionality();
        testPoolExhaustion();
        testGlobalPool();
        runPerformanceComparison();
        runMultiThreadedTest();

        std::cout << "\n🎉 All tests completed successfully!\n";

        // Cleanup global pool
        GlobalMessagePool::shutdown();

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}