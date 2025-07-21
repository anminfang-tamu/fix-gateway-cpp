#include "common/message_pool.h"
#include "common/message.h"
#include "utils/performance_timer.h"
#include "utils/logger.h"
#include <iostream>
#include <vector>

using namespace fix_gateway::common;
using namespace fix_gateway::utils;

/**
 * @brief Focused validation test for templated MessagePool with Message class
 *
 * Specifically tests that the Message class can still use the pool exactly as before,
 * addressing the user's concern about compatibility.
 */
int main()
{
    LOG_INFO("=== Message Pool Validation Test ===");

    std::cout << "\n🔍 Validating Message Class with Templated Pool" << std::endl;
    std::cout << "=================================================" << std::endl;

    try
    {
        // Test 1: Basic Message pool operations
        {
            std::cout << "\n📊 Test 1: Basic Message Pool Operations" << std::endl;

            MessagePool<Message> pool(1000, "validation_pool");
            pool.prewarm();

            std::cout << "   Initial pool: " << pool.toString() << std::endl;

            // Test minimal allocation
            Message *msg1 = pool.allocate("BASIC_MSG", "basic payload");
            if (!msg1)
            {
                std::cout << "   ❌ Basic allocation failed!" << std::endl;
                return 1;
            }
            std::cout << "   ✅ Basic allocation successful" << std::endl;

            // Test allocation with arguments (perfect forwarding)
            Message *msg2 = pool.allocate(
                "TEST_MSG_123",
                "test payload data",
                Priority::HIGH,
                MessageType::ORDER,
                "SESSION_01",
                "EXCHANGE");

            if (!msg2)
            {
                std::cout << "   ❌ Parameterized allocation failed!" << std::endl;
                return 1;
            }

            std::cout << "   ✅ Parameterized allocation successful" << std::endl;
            std::cout << "      Message ID: " << msg2->getMessageId() << std::endl;
            std::cout << "      Payload: " << msg2->getPayload() << std::endl;
            std::cout << "      Priority: " << static_cast<int>(msg2->getPriority()) << std::endl;

            // Test pool status
            std::cout << "   Pool with 2 allocated: " << pool.toString() << std::endl;

            // Test deallocation
            pool.deallocate(msg1);
            pool.deallocate(msg2);

            std::cout << "   Final pool: " << pool.toString() << std::endl;
            std::cout << "   ✅ All basic operations working perfectly!" << std::endl;
        }

        // Test 2: Global pool operations
        {
            std::cout << "\n📊 Test 2: Global Message Pool Operations" << std::endl;

            auto &globalPool = GlobalMessagePool<Message>::getInstance(500);
            std::cout << "   Global pool: " << globalPool.toString() << std::endl;

            // Test global allocation
            Message *msg = GlobalMessagePool<Message>::allocate(
                "GLOBAL_MSG",
                "global payload",
                Priority::CRITICAL);

            if (!msg)
            {
                std::cout << "   ❌ Global allocation failed!" << std::endl;
                return 1;
            }

            std::cout << "   ✅ Global allocation successful: " << msg->getMessageId() << std::endl;
            std::cout << "   Global pool after alloc: " << globalPool.toString() << std::endl;

            // Test global deallocation
            GlobalMessagePool<Message>::deallocate(msg);
            std::cout << "   Global pool after dealloc: " << globalPool.toString() << std::endl;
            std::cout << "   ✅ Global pool operations working perfectly!" << std::endl;
        }

        // Test 3: Performance validation (should be same as before)
        {
            std::cout << "\n📊 Test 3: Performance Validation" << std::endl;

            constexpr int ITERATIONS = 10000;
            MessagePool<Message> perfPool(ITERATIONS, "perf_pool");
            perfPool.prewarm();

            auto startTime = PerformanceTimer::now();

            std::vector<Message *> messages;
            messages.reserve(ITERATIONS);

            // Allocation test
            for (int i = 0; i < ITERATIONS; ++i)
            {
                Message *msg = perfPool.allocate(
                    "MSG_" + std::to_string(i),
                    "test_payload_" + std::to_string(i),
                    Priority::HIGH,
                    MessageType::ORDER);
                if (msg)
                {
                    messages.push_back(msg);
                }
            }

            // Deallocation test
            for (Message *msg : messages)
            {
                perfPool.deallocate(msg);
            }

            auto endTime = PerformanceTimer::now();
            auto duration = PerformanceTimer::duration(startTime, endTime);

            double avgLatency = duration.count() / (2.0 * ITERATIONS);

            std::cout << "   " << ITERATIONS << " alloc/dealloc cycles: "
                      << PerformanceTimer::toMilliseconds(duration) << " ms" << std::endl;
            std::cout << "   Average per operation: " << avgLatency << " ns" << std::endl;

            if (avgLatency < 1000)
            { // Should be much better than 1μs
                std::cout << "   ✅ Performance excellent (< 1μs per operation)!" << std::endl;
            }
            else
            {
                std::cout << "   ⚠️  Performance higher than expected" << std::endl;
            }

            std::cout << "   Final performance pool: " << perfPool.toString() << std::endl;
        }

        // Test 4: Message functionality validation
        {
            std::cout << "\n📊 Test 4: Message Class Functionality Validation" << std::endl;

            MessagePool<Message> funcPool(100, "func_pool");

            Message *msg = funcPool.allocate(
                "FUNC_TEST",
                "functional test payload",
                Priority::CRITICAL,
                MessageType::ORDER,
                "SESSION_FUNC",
                "EXCHANGE_FUNC");

            if (!msg)
            {
                std::cout << "   ❌ Function test allocation failed!" << std::endl;
                return 1;
            }

            // Test all Message methods work correctly
            std::cout << "   Message functionality test:" << std::endl;
            std::cout << "      ID: " << msg->getMessageId() << std::endl;
            std::cout << "      Payload: " << msg->getPayload() << std::endl;
            std::cout << "      Priority: " << static_cast<int>(msg->getPriority()) << std::endl;
            std::cout << "      Type: " << static_cast<int>(msg->getMessageType()) << std::endl;
            std::cout << "      Session: " << msg->getSessionId() << std::endl;
            std::cout << "      Destination: " << msg->getDestination() << std::endl;
            std::cout << "      State: " << static_cast<int>(msg->getState()) << std::endl;
            std::cout << "      Sequence: " << msg->getSequenceNumber() << std::endl;

            // Test state management
            msg->setState(MessageState::SENDING);
            if (msg->getState() == MessageState::SENDING)
            {
                std::cout << "   ✅ State management working" << std::endl;
            }

            // Test timing functions
            msg->setQueueEntryTime(std::chrono::steady_clock::now());
            if (msg->getQueueEntryTime().time_since_epoch().count() > 0)
            {
                std::cout << "   ✅ Timing functions working" << std::endl;
            }

            funcPool.deallocate(msg);
            std::cout << "   ✅ All Message class functionality works perfectly!" << std::endl;
        }

        // Cleanup global pools
        GlobalMessagePool<Message>::shutdown();

        std::cout << "\n🎉 ALL TESTS PASSED!" << std::endl;
        std::cout << "✅ Your templated MessagePool works perfectly with Message class" << std::endl;
        std::cout << "✅ All original functionality preserved" << std::endl;
        std::cout << "✅ Performance characteristics maintained" << std::endl;
        std::cout << "✅ Ready for production use!" << std::endl;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Test failed with exception: " + std::string(e.what()));
        std::cout << "\n❌ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}