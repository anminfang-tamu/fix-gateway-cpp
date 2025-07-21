#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "utils/performance_timer.h"
#include "utils/logger.h"
#include <iostream>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <queue>

using namespace fix_gateway;
using namespace fix_gateway::protocol;

/**
 * @brief Memory management performance benchmark for trading systems
 *
 * Tests different memory management strategies for FIX messages:
 * 1. shared_ptr (current approach)
 * 2. unique_ptr (move-only semantics)
 * 3. Raw pointers with object pool
 * 4. Stack allocation with copying
 */
class MemoryPerformanceBenchmark
{
public:
    static constexpr int ITERATIONS = 100000;
    static constexpr int THREAD_COUNT = 4;

    void runAllBenchmarks()
    {
        std::cout << "\n=== Memory Management Performance Benchmark ===" << std::endl;
        std::cout << "Testing " << ITERATIONS << " iterations per test\n"
                  << std::endl;

        // Single-threaded benchmarks
        benchmarkSharedPtr();
        benchmarkUniquePtr();
        benchmarkRawPointer();
        benchmarkStackAllocation();

        // Multi-threaded benchmarks (simulates trading system)
        std::cout << "\n=== Multi-threaded Performance (4 threads) ===" << std::endl;
        benchmarkMultiThreadedSharedPtr();
        benchmarkMultiThreadedUniquePtr();
        benchmarkMultiThreadedObjectPool();

        std::cout << "\n=== Memory Overhead Analysis ===" << std::endl;
        analyzeMemoryOverhead();
    }

private:
    void benchmarkSharedPtr()
    {
        std::cout << "📊 shared_ptr Performance:" << std::endl;

        std::vector<std::shared_ptr<FixMessage>> messages;
        messages.reserve(ITERATIONS);

        // Test creation + copying (simulates message routing)
        auto startTime = utils::PerformanceTimer::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            auto msg = std::make_shared<FixMessage>();
            msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
            msg->setField(FixFields::ClOrdID, "ORDER_" + std::to_string(i));
            msg->setField(FixFields::Symbol, "AAPL");
            msg->setField(FixFields::Side, OrderSide::Buy);
            msg->setField(FixFields::OrderQty, "100");

            // Simulate message passing through system (3 copies)
            auto copy1 = msg; // To MessageManager
            auto copy2 = msg; // To Priority Queue
            auto copy3 = msg; // To AsyncSender

            messages.push_back(copy3);
        }

        auto endTime = utils::PerformanceTimer::now();
        auto duration = utils::PerformanceTimer::duration(startTime, endTime);

        std::cout << "   Creation + 3 copies: " << duration.count() / ITERATIONS << " ns/message" << std::endl;
        std::cout << "   Reference count operations: " << (duration.count() / ITERATIONS) / 4 << " ns/operation" << std::endl;
    }

    void benchmarkUniquePtr()
    {
        std::cout << "📊 unique_ptr Performance:" << std::endl;

        std::vector<std::unique_ptr<FixMessage>> messages;
        messages.reserve(ITERATIONS);

        auto startTime = utils::PerformanceTimer::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            auto msg = std::make_unique<FixMessage>();
            msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
            msg->setField(FixFields::ClOrdID, "ORDER_" + std::to_string(i));
            msg->setField(FixFields::Symbol, "AAPL");
            msg->setField(FixFields::Side, OrderSide::Buy);
            msg->setField(FixFields::OrderQty, "100");

            // Simulate move-only semantics through system
            messages.push_back(std::move(msg));
        }

        auto endTime = utils::PerformanceTimer::now();
        auto duration = utils::PerformanceTimer::duration(startTime, endTime);

        std::cout << "   Creation + moves: " << duration.count() / ITERATIONS << " ns/message" << std::endl;
    }

    void benchmarkRawPointer()
    {
        std::cout << "📊 Raw Pointer Performance:" << std::endl;

        std::vector<FixMessage *> messages;
        messages.reserve(ITERATIONS);

        auto startTime = utils::PerformanceTimer::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            auto *msg = new FixMessage();
            msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
            msg->setField(FixFields::ClOrdID, "ORDER_" + std::to_string(i));
            msg->setField(FixFields::Symbol, "AAPL");
            msg->setField(FixFields::Side, OrderSide::Buy);
            msg->setField(FixFields::OrderQty, "100");

            messages.push_back(msg);
        }

        auto endTime = utils::PerformanceTimer::now();
        auto duration = utils::PerformanceTimer::duration(startTime, endTime);

        std::cout << "   Creation + raw pointer: " << duration.count() / ITERATIONS << " ns/message" << std::endl;

        // Cleanup (in real system, object pool would handle this)
        for (auto *msg : messages)
        {
            delete msg;
        }
    }

    void benchmarkStackAllocation()
    {
        std::cout << "📊 Stack Allocation Performance:" << std::endl;

        auto startTime = utils::PerformanceTimer::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            FixMessage msg; // Stack allocated
            msg.setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
            msg.setField(FixFields::ClOrdID, "ORDER_" + std::to_string(i));
            msg.setField(FixFields::Symbol, "AAPL");
            msg.setField(FixFields::Side, OrderSide::Buy);
            msg.setField(FixFields::OrderQty, "100");

            // Simulate copying through system
            FixMessage copy1 = msg;
            FixMessage copy2 = copy1;

            // Prevent optimization
            volatile auto size = copy2.getFieldCount();
            (void)size;
        }

        auto endTime = utils::PerformanceTimer::now();
        auto duration = utils::PerformanceTimer::duration(startTime, endTime);

        std::cout << "   Stack + copying: " << duration.count() / ITERATIONS << " ns/message" << std::endl;
    }

    void benchmarkMultiThreadedSharedPtr()
    {
        std::cout << "📊 Multi-threaded shared_ptr (simulates trading system):" << std::endl;

        std::atomic<uint64_t> totalTime{0};
        std::vector<std::thread> threads;

        for (int t = 0; t < THREAD_COUNT; ++t)
        {
            threads.emplace_back([&, t]()
                                 {
                auto startTime = utils::PerformanceTimer::now();
                
                for (int i = 0; i < ITERATIONS / THREAD_COUNT; ++i)
                {
                    // Simulate message creation (producer)
                    auto msg = std::make_shared<FixMessage>();
                    msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
                    msg->setField(FixFields::ClOrdID, "T" + std::to_string(t) + "_" + std::to_string(i));
                    
                    // Simulate message routing (atomic reference counting)
                    std::vector<std::shared_ptr<FixMessage>> copies;
                    for (int c = 0; c < 3; ++c) {
                        copies.push_back(msg);  // Atomic increment
                    }
                    
                    // Simulate processing (atomic decrements as copies go out of scope)
                }
                
                auto endTime = utils::PerformanceTimer::now();
                auto duration = utils::PerformanceTimer::duration(startTime, endTime);
                totalTime.fetch_add(duration.count(), std::memory_order_relaxed); });
        }

        for (auto &thread : threads)
        {
            thread.join();
        }

        std::cout << "   Multi-threaded shared_ptr: " << totalTime.load() / ITERATIONS << " ns/message" << std::endl;
        std::cout << "   Atomic contention overhead: " << (totalTime.load() / ITERATIONS) / 4 << " ns/ref operation" << std::endl;
    }

    void benchmarkMultiThreadedUniquePtr()
    {
        std::cout << "📊 Multi-threaded unique_ptr with manual lifecycle:" << std::endl;

        std::atomic<uint64_t> totalTime{0};
        std::vector<std::thread> threads;

        for (int t = 0; t < THREAD_COUNT; ++t)
        {
            threads.emplace_back([&, t]()
                                 {
                auto startTime = utils::PerformanceTimer::now();
                
                for (int i = 0; i < ITERATIONS / THREAD_COUNT; ++i)
                {
                    // Simulate message creation and move-only semantics
                    auto msg = std::make_unique<FixMessage>();
                    msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
                    msg->setField(FixFields::ClOrdID, "T" + std::to_string(t) + "_" + std::to_string(i));
                    
                    // Simulate move through system (no atomic operations)
                    auto moved = std::move(msg);
                    auto moved2 = std::move(moved);
                    
                    // Simulate processing
                    volatile auto size = moved2->getFieldCount();
                    (void)size;
                }
                
                auto endTime = utils::PerformanceTimer::now();
                auto duration = utils::PerformanceTimer::duration(startTime, endTime);
                totalTime.fetch_add(duration.count(), std::memory_order_relaxed); });
        }

        for (auto &thread : threads)
        {
            thread.join();
        }

        std::cout << "   Multi-threaded unique_ptr: " << totalTime.load() / ITERATIONS << " ns/message" << std::endl;
    }

    void benchmarkMultiThreadedObjectPool()
    {
        std::cout << "📊 Multi-threaded Object Pool:" << std::endl;

        // Simple object pool simulation
        std::queue<FixMessage *> objectPool;
        std::mutex poolMutex;

        // Pre-populate pool
        for (int i = 0; i < 1000; ++i)
        {
            objectPool.push(new FixMessage());
        }

        std::atomic<uint64_t> totalTime{0};
        std::vector<std::thread> threads;

        for (int t = 0; t < THREAD_COUNT; ++t)
        {
            threads.emplace_back([&, t]()
                                 {
                auto startTime = utils::PerformanceTimer::now();
                std::vector<FixMessage*> usedMessages;
                
                for (int i = 0; i < ITERATIONS / THREAD_COUNT; ++i)
                {
                    // Get from pool
                    FixMessage* msg = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(poolMutex);
                        if (!objectPool.empty()) {
                            msg = objectPool.front();
                            objectPool.pop();
                        }
                    }
                    
                    if (!msg) {
                        msg = new FixMessage();  // Fallback allocation
                    }
                    
                    // Use message
                    msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
                    msg->setField(FixFields::ClOrdID, "T" + std::to_string(t) + "_" + std::to_string(i));
                    
                    usedMessages.push_back(msg);
                }
                
                // Return to pool
                {
                    std::lock_guard<std::mutex> lock(poolMutex);
                    for (auto* msg : usedMessages) {
                        objectPool.push(msg);
                    }
                }
                
                auto endTime = utils::PerformanceTimer::now();
                auto duration = utils::PerformanceTimer::duration(startTime, endTime);
                totalTime.fetch_add(duration.count(), std::memory_order_relaxed); });
        }

        for (auto &thread : threads)
        {
            thread.join();
        }

        std::cout << "   Object pool: " << totalTime.load() / ITERATIONS << " ns/message" << std::endl;

        // Cleanup pool
        while (!objectPool.empty())
        {
            delete objectPool.front();
            objectPool.pop();
        }
    }

    void analyzeMemoryOverhead()
    {
        std::cout << "📊 Memory Overhead Analysis:" << std::endl;

        // Measure memory sizes
        std::cout << "   FixMessage size: " << sizeof(FixMessage) << " bytes" << std::endl;
        std::cout << "   shared_ptr size: " << sizeof(std::shared_ptr<FixMessage>) << " bytes" << std::endl;
        std::cout << "   unique_ptr size: " << sizeof(std::unique_ptr<FixMessage>) << " bytes" << std::endl;
        std::cout << "   raw pointer size: " << sizeof(FixMessage *) << " bytes" << std::endl;

        // Estimate control block overhead
        std::cout << "   Estimated shared_ptr control block: 24-32 bytes" << std::endl;
        std::cout << "   Total shared_ptr overhead: ~" << sizeof(std::shared_ptr<FixMessage>) + 28 << " bytes" << std::endl;
    }
};

int main()
{
    LOG_INFO("=== Memory Performance Analysis for Trading System ===");

    try
    {
        MemoryPerformanceBenchmark benchmark;
        benchmark.runAllBenchmarks();

        std::cout << "\n🎯 Recommendations for Trading System:" << std::endl;
        std::cout << "  • shared_ptr: Good for multi-threaded safety, but atomic overhead" << std::endl;
        std::cout << "  • unique_ptr: Best single-owner performance, requires careful lifecycle" << std::endl;
        std::cout << "  • Object pools: Optimal for high-frequency scenarios" << std::endl;
        std::cout << "  • Stack allocation: Fastest but limited to synchronous processing" << std::endl;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception in memory benchmark: " + std::string(e.what()));
        return 1;
    }

    return 0;
}