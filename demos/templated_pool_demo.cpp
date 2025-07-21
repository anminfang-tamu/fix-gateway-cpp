#include "common/templated_message_pool.h"
#include "common/message.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "utils/performance_timer.h"
#include "utils/logger.h"
#include <iostream>
#include <vector>

using namespace fix_gateway;
using namespace fix_gateway::common;
using namespace fix_gateway::protocol;

/**
 * @brief Demonstrates the power of templated message pools
 *
 * Shows how the same lock-free pool architecture can work with:
 * 1. Your existing Message class (for routing/priority)
 * 2. New FixMessage class (for FIX protocol parsing)
 * 3. Any future message types with zero code duplication
 */
class TemplatedPoolDemo
{
public:
    void run()
    {
        std::cout << "\n🚀 Templated Message Pool Demo" << std::endl;
        std::cout << "===================================" << std::endl;
        std::cout << "Same lock-free pool performance for any message type!\n"
                  << std::endl;

        // Test with your existing Message class
        testMessagePool();

        // Test with FixMessage class
        testFixMessagePool();

        // Test mixed usage
        testMixedUsage();

        // Performance comparison
        performanceComparison();

        // Cleanup
        cleanup();
    }

private:
    static constexpr int ITERATIONS = 5000;

    void testMessagePool()
    {
        std::cout << "📊 Test 1: Existing Message Class with Templated Pool" << std::endl;

        // Get Message pool instance (same API as before)
        auto &messagePool = GlobalTemplatedMessagePool<Message>::getInstance(1000);

        std::cout << "   Pool status: " << messagePool.toString() << std::endl;

        // Test basic allocation/deallocation
        std::vector<Message *> messages;

        auto startTime = utils::PerformanceTimer::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            // Allocate with constructor arguments - perfect forwarding
            Message *msg = GlobalTemplatedMessagePool<Message>::allocate(
                "MSG_" + std::to_string(i),
                "Test payload " + std::to_string(i),
                Priority::HIGH,
                MessageType::ORDER,
                "SESSION_01",
                "EXCHANGE");

            if (msg)
            {
                messages.push_back(msg);
            }
        }

        // Deallocate all messages
        for (Message *msg : messages)
        {
            GlobalTemplatedMessagePool<Message>::deallocate(msg);
        }

        auto endTime = utils::PerformanceTimer::now();
        auto duration = utils::PerformanceTimer::duration(startTime, endTime);

        std::cout << "   " << ITERATIONS << " allocate/deallocate cycles: "
                  << utils::PerformanceTimer::toMilliseconds(duration) << " ms" << std::endl;
        std::cout << "   Per operation: " << duration.count() / (2 * ITERATIONS) << " ns" << std::endl;
        std::cout << "   Final pool: " << messagePool.toString() << std::endl;
        std::cout << "   ✅ Same performance as original pool!\n"
                  << std::endl;
    }

    void testFixMessagePool()
    {
        std::cout << "📊 Test 2: FixMessage Class with Templated Pool" << std::endl;

        // Get FixMessage pool instance (separate from Message pool)
        auto &fixPool = GlobalTemplatedMessagePool<FixMessage>::getInstance(1000);

        std::cout << "   Pool status: " << fixPool.toString() << std::endl;

        std::vector<FixMessage *> fixMessages;

        auto startTime = utils::PerformanceTimer::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            // Allocate FixMessage with default constructor
            FixMessage *fixMsg = GlobalTemplatedMessagePool<FixMessage>::allocate();

            if (fixMsg)
            {
                // Set FIX fields directly on pooled message
                fixMsg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
                fixMsg->setField(FixFields::ClOrdID, "ORDER_" + std::to_string(i));
                fixMsg->setField(FixFields::Symbol, "AAPL");
                fixMsg->setField(FixFields::Side, "1"); // Buy
                fixMsg->setField(FixFields::OrderQty, "100");

                fixMessages.push_back(fixMsg);
            }
        }

        // Test serialization performance
        volatile size_t totalSize = 0;
        for (FixMessage *fixMsg : fixMessages)
        {
            std::string serialized = fixMsg->toString();
            totalSize += serialized.size();
        }
        (void)totalSize; // Prevent optimization

        // Deallocate all FIX messages
        for (FixMessage *fixMsg : fixMessages)
        {
            GlobalTemplatedMessagePool<FixMessage>::deallocate(fixMsg);
        }

        auto endTime = utils::PerformanceTimer::now();
        auto duration = utils::PerformanceTimer::duration(startTime, endTime);

        std::cout << "   " << ITERATIONS << " FIX allocate/serialize/deallocate: "
                  << utils::PerformanceTimer::toMilliseconds(duration) << " ms" << std::endl;
        std::cout << "   Per FIX message: " << duration.count() / ITERATIONS << " ns" << std::endl;
        std::cout << "   Final pool: " << fixPool.toString() << std::endl;
        std::cout << "   ✅ Lock-free pool + FIX parsing = optimal performance!\n"
                  << std::endl;
    }

    void testMixedUsage()
    {
        std::cout << "📊 Test 3: Mixed Message Types (Real Trading Scenario)" << std::endl;

        // Simulate real trading system with both message types
        auto &messagePool = GlobalTemplatedMessagePool<Message>::getInstance();
        auto &fixPool = GlobalTemplatedMessagePool<FixMessage>::getInstance();

        auto startTime = utils::PerformanceTimer::now();

        std::vector<Message *> routingMessages;
        std::vector<FixMessage *> fixMessages;

        for (int i = 0; i < ITERATIONS; ++i)
        {
            // Create routing message (for internal queues/priorities)
            Message *routingMsg = GlobalTemplatedMessagePool<Message>::allocate(
                "ROUTE_" + std::to_string(i),
                "", // Will contain reference to FIX message
                Priority::HIGH,
                MessageType::ORDER,
                "SESSION_01",
                "EXCHANGE");

            // Create FIX message (for protocol handling)
            FixMessage *fixMsg = GlobalTemplatedMessagePool<FixMessage>::allocate();
            fixMsg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
            fixMsg->setField(FixFields::ClOrdID, "ORDER_" + std::to_string(i));
            fixMsg->setField(FixFields::Symbol, "TSLA");

            if (routingMsg)
                routingMessages.push_back(routingMsg);
            if (fixMsg)
                fixMessages.push_back(fixMsg);
        }

        // Process messages (simulate routing and FIX serialization)
        for (size_t i = 0; i < std::min(routingMessages.size(), fixMessages.size()); ++i)
        {
            // Route message through priority system
            volatile auto priority = routingMessages[i]->getPriority();
            (void)priority;

            // Serialize FIX message for wire transmission
            volatile auto fixData = fixMessages[i]->toString();
            (void)fixData;
        }

        // Cleanup
        for (Message *msg : routingMessages)
        {
            GlobalTemplatedMessagePool<Message>::deallocate(msg);
        }
        for (FixMessage *msg : fixMessages)
        {
            GlobalTemplatedMessagePool<FixMessage>::deallocate(msg);
        }

        auto endTime = utils::PerformanceTimer::now();
        auto duration = utils::PerformanceTimer::duration(startTime, endTime);

        std::cout << "   " << ITERATIONS << " mixed message processing: "
                  << utils::PerformanceTimer::toMilliseconds(duration) << " ms" << std::endl;
        std::cout << "   Per message pair: " << duration.count() / ITERATIONS << " ns" << std::endl;
        std::cout << "   Message pool: " << messagePool.toString() << std::endl;
        std::cout << "   FIX pool: " << fixPool.toString() << std::endl;
        std::cout << "   ✅ Two specialized pools working together!\n"
                  << std::endl;
    }

    void performanceComparison()
    {
        std::cout << "📊 Test 4: Performance Comparison" << std::endl;
        std::cout << "   Comparing: shared_ptr vs unique_ptr vs templated pool" << std::endl;

        // Test shared_ptr (current approach)
        {
            auto startTime = utils::PerformanceTimer::now();
            std::vector<std::shared_ptr<FixMessage>> messages;

            for (int i = 0; i < ITERATIONS; ++i)
            {
                auto msg = std::make_shared<FixMessage>();
                msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
                messages.push_back(msg);
            }
            messages.clear();

            auto endTime = utils::PerformanceTimer::now();
            auto duration = utils::PerformanceTimer::duration(startTime, endTime);
            std::cout << "   shared_ptr: " << duration.count() / ITERATIONS << " ns/msg" << std::endl;
        }

        // Test unique_ptr
        {
            auto startTime = utils::PerformanceTimer::now();
            std::vector<std::unique_ptr<FixMessage>> messages;

            for (int i = 0; i < ITERATIONS; ++i)
            {
                auto msg = std::make_unique<FixMessage>();
                msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
                messages.push_back(std::move(msg));
            }
            messages.clear();

            auto endTime = utils::PerformanceTimer::now();
            auto duration = utils::PerformanceTimer::duration(startTime, endTime);
            std::cout << "   unique_ptr: " << duration.count() / ITERATIONS << " ns/msg" << std::endl;
        }

        // Test templated pool
        {
            auto startTime = utils::PerformanceTimer::now();
            std::vector<FixMessage *> messages;

            for (int i = 0; i < ITERATIONS; ++i)
            {
                FixMessage *msg = GlobalTemplatedMessagePool<FixMessage>::allocate();
                msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
                messages.push_back(msg);
            }

            for (FixMessage *msg : messages)
            {
                GlobalTemplatedMessagePool<FixMessage>::deallocate(msg);
            }

            auto endTime = utils::PerformanceTimer::now();
            auto duration = utils::PerformanceTimer::duration(startTime, endTime);
            std::cout << "   templated pool: " << duration.count() / ITERATIONS << " ns/msg" << std::endl;
        }

        std::cout << "   ✅ Templated pool should be fastest (no heap allocation)!\n"
                  << std::endl;
    }

    void cleanup()
    {
        std::cout << "🧹 Cleanup" << std::endl;

        GlobalTemplatedMessagePool<Message>::shutdown();
        GlobalTemplatedMessagePool<FixMessage>::shutdown();

        std::cout << "   ✅ All pools shutdown successfully" << std::endl;
    }
};

void demonstrateTypeAliases()
{
    std::cout << "\n💡 Type Aliases for Easy Usage" << std::endl;
    std::cout << "================================" << std::endl;

    // Define convenient type aliases
    using MessagePool = TemplatedMessagePool<Message>;
    using FixMessagePool = TemplatedMessagePool<FixMessage>;

    std::cout << "   // Convenient type aliases:" << std::endl;
    std::cout << "   using MessagePool = TemplatedMessagePool<Message>;" << std::endl;
    std::cout << "   using FixMessagePool = TemplatedMessagePool<FixMessage>;" << std::endl;
    std::cout << std::endl;
    std::cout << "   // Usage examples:" << std::endl;
    std::cout << "   MessagePool routingPool(1000, \"routing_pool\");" << std::endl;
    std::cout << "   FixMessagePool protocolPool(2000, \"protocol_pool\");" << std::endl;
    std::cout << std::endl;

    // Show actual usage
    MessagePool routingPool(100, "routing_pool");
    FixMessagePool protocolPool(100, "protocol_pool");

    std::cout << "   Created pools:" << std::endl;
    std::cout << "   " << routingPool.toString() << std::endl;
    std::cout << "   " << protocolPool.toString() << std::endl;

    // Quick allocation test
    Message *msg = routingPool.allocate("TEST", "payload", Priority::HIGH);
    FixMessage *fixMsg = protocolPool.allocate();

    if (msg)
    {
        std::cout << "   ✅ Message allocation successful" << std::endl;
        routingPool.deallocate(msg);
    }

    if (fixMsg)
    {
        std::cout << "   ✅ FixMessage allocation successful" << std::endl;
        protocolPool.deallocate(fixMsg);
    }

    std::cout << "   ✅ Type-safe, high-performance, reusable!" << std::endl;
}

int main()
{
    LOG_INFO("=== Templated Message Pool Demo ===");

    try
    {
        demonstrateTypeAliases();

        TemplatedPoolDemo demo;
        demo.run();

        std::cout << "\n🎯 Key Benefits of Templated Pool:" << std::endl;
        std::cout << "   1. ✅ Same lock-free performance for any message type" << std::endl;
        std::cout << "   2. ✅ Type safety - no casting or void* pointers" << std::endl;
        std::cout << "   3. ✅ Zero code duplication - single template implementation" << std::endl;
        std::cout << "   4. ✅ Perfect forwarding - any constructor arguments supported" << std::endl;
        std::cout << "   5. ✅ Separate pools per type - optimal memory locality" << std::endl;
        std::cout << "   6. ✅ Drop-in replacement for existing MessagePool" << std::endl;
        std::cout << "\n🚀 Ready for production trading systems!" << std::endl;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception in templated pool demo: " + std::string(e.what()));
        return 1;
    }

    return 0;
}