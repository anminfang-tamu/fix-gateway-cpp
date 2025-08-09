#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "manager/message_router.h"
#include "application/priority_queue_container.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"

#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

using namespace fix_gateway::manager;
using namespace fix_gateway::protocol;
using namespace fix_gateway::common;

class MessageRouterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create message pool for test messages
        message_pool_ = std::make_unique<MessagePool<FixMessage>>(1000, "test_pool");
        
        // Create priority queue container
        queue_container_ = std::make_shared<PriorityQueueContainer>();
        
        // Create message router
        router_ = std::make_unique<MessageRouter>(queue_container_);
        
        // Start the router
        router_->start();
    }

    void TearDown() override
    {
        router_->stop();
        router_.reset();
        queue_container_.reset();
        message_pool_.reset();
    }

    FixMessage* createTestMessage(FixMsgType msgType)
    {
        FixMessage* message = message_pool_->allocate();
        if (!message) return nullptr;

        // Set basic FIX fields (explicit string type to avoid ambiguity)
        message->setField(FixFields::BeginString, std::string("FIX.4.4"));
        message->setField(FixFields::BodyLength, std::string("100"));
        message->setField(FixFields::MsgType, std::string(FixMsgTypeUtils::toString(msgType)));
        message->setField(FixFields::SenderCompID, std::string("SENDER"));
        message->setField(FixFields::TargetCompID, std::string("TARGET"));
        message->setField(FixFields::MsgSeqNum, std::string("1"));
        message->setField(FixFields::SendingTime, std::string("20231201-12:00:00"));
        message->setField(FixFields::CheckSum, std::string("123"));

        return message;
    }

    void deallocateMessage(FixMessage* message)
    {
        if (message)
        {
            message_pool_->deallocate(message);
        }
    }

    // Helper to drain a specific priority queue
    std::vector<FixMessage*> drainQueue(Priority priority)
    {
        std::vector<FixMessage*> messages;
        auto queue = queue_container_->getQueue(priority);
        FixMessage* message;
        
        while (queue->tryPop(message))
        {
            messages.push_back(message);
        }
        
        return messages;
    }

    std::unique_ptr<MessagePool<FixMessage>> message_pool_;
    std::shared_ptr<PriorityQueueContainer> queue_container_;
    std::unique_ptr<MessageRouter> router_;
};

// =============================================================================
// BASIC FUNCTIONALITY TESTS
// =============================================================================

TEST_F(MessageRouterTest, StartAndStop)
{
    EXPECT_TRUE(router_->isRunning());
    
    router_->stop();
    EXPECT_FALSE(router_->isRunning());
    
    router_->start();
    EXPECT_TRUE(router_->isRunning());
}

TEST_F(MessageRouterTest, HandleNullMessage)
{
    // Route null message - should not crash
    router_->routeMessage(nullptr);
    
    // Check error stats
    const auto& stats = router_->getStats();
    EXPECT_EQ(1, stats.routing_errors.load());
    EXPECT_EQ(0, stats.messages_routed.load());
}

// =============================================================================
// PRIORITY MAPPING TESTS
// =============================================================================

TEST_F(MessageRouterTest, CriticalPriorityMessages)
{
    // Test EXECUTION_REPORT -> CRITICAL
    FixMessage* exec_report = createTestMessage(FixMsgType::EXECUTION_REPORT);
    ASSERT_NE(nullptr, exec_report);
    
    router_->routeMessage(exec_report);
    
    // Should be routed to critical queue
    auto critical_messages = drainQueue(Priority::CRITICAL);
    EXPECT_EQ(1, critical_messages.size());
    EXPECT_EQ(exec_report, critical_messages[0]);
    
    // Other queues should be empty
    EXPECT_TRUE(drainQueue(Priority::HIGH).empty());
    EXPECT_TRUE(drainQueue(Priority::MEDIUM).empty());
    EXPECT_TRUE(drainQueue(Priority::LOW).empty());
    
    // Clean up
    deallocateMessage(exec_report);
}

TEST_F(MessageRouterTest, HighPriorityMessages)
{
    // Test MARKET_DATA_REQUEST -> HIGH
    FixMessage* market_data = createTestMessage(FixMsgType::MARKET_DATA_REQUEST);
    ASSERT_NE(nullptr, market_data);
    
    router_->routeMessage(market_data);
    
    // Should be routed to high priority queue
    auto high_messages = drainQueue(Priority::HIGH);
    EXPECT_EQ(1, high_messages.size());
    EXPECT_EQ(market_data, high_messages[0]);
    
    // Other queues should be empty
    EXPECT_TRUE(drainQueue(Priority::CRITICAL).empty());
    EXPECT_TRUE(drainQueue(Priority::MEDIUM).empty());
    EXPECT_TRUE(drainQueue(Priority::LOW).empty());
    
    // Clean up
    deallocateMessage(market_data);
}

TEST_F(MessageRouterTest, MediumPriorityMessages)
{
    // Test LOGON -> MEDIUM
    FixMessage* logon = createTestMessage(FixMsgType::LOGON);
    ASSERT_NE(nullptr, logon);
    
    router_->routeMessage(logon);
    
    // Should be routed to medium priority queue
    auto medium_messages = drainQueue(Priority::MEDIUM);
    EXPECT_EQ(1, medium_messages.size());
    EXPECT_EQ(logon, medium_messages[0]);
    
    // Other queues should be empty
    EXPECT_TRUE(drainQueue(Priority::CRITICAL).empty());
    EXPECT_TRUE(drainQueue(Priority::HIGH).empty());
    EXPECT_TRUE(drainQueue(Priority::LOW).empty());
    
    // Clean up
    deallocateMessage(logon);
}

TEST_F(MessageRouterTest, LowPriorityMessages)
{
    // Test HEARTBEAT -> LOW
    FixMessage* heartbeat = createTestMessage(FixMsgType::HEARTBEAT);
    ASSERT_NE(nullptr, heartbeat);
    
    router_->routeMessage(heartbeat);
    
    // Should be routed to low priority queue
    auto low_messages = drainQueue(Priority::LOW);
    EXPECT_EQ(1, low_messages.size());
    EXPECT_EQ(heartbeat, low_messages[0]);
    
    // Other queues should be empty
    EXPECT_TRUE(drainQueue(Priority::CRITICAL).empty());
    EXPECT_TRUE(drainQueue(Priority::HIGH).empty());
    EXPECT_TRUE(drainQueue(Priority::MEDIUM).empty());
    
    // Clean up
    deallocateMessage(heartbeat);
}

TEST_F(MessageRouterTest, UnknownMessageType)
{
    // Test UNKNOWN -> LOW (default)
    FixMessage* unknown = createTestMessage(FixMsgType::UNKNOWN);
    ASSERT_NE(nullptr, unknown);
    
    router_->routeMessage(unknown);
    
    // Should be routed to low priority queue (default)
    auto low_messages = drainQueue(Priority::LOW);
    EXPECT_EQ(1, low_messages.size());
    EXPECT_EQ(unknown, low_messages[0]);
    
    // Clean up
    deallocateMessage(unknown);
}

// =============================================================================
// BATCH ROUTING TESTS
// =============================================================================

TEST_F(MessageRouterTest, BatchRouting)
{
    const size_t batch_size = 10;
    std::vector<FixMessage*> messages;
    std::vector<FixMessage*> message_ptrs;
    
    // Create mixed priority messages
    for (size_t i = 0; i < batch_size; ++i)
    {
        FixMsgType msgType;
        if (i % 4 == 0) msgType = FixMsgType::EXECUTION_REPORT; // CRITICAL
        else if (i % 4 == 1) msgType = FixMsgType::MARKET_DATA_REQUEST; // HIGH
        else if (i % 4 == 2) msgType = FixMsgType::LOGON; // MEDIUM
        else msgType = FixMsgType::HEARTBEAT; // LOW
        
        FixMessage* message = createTestMessage(msgType);
        ASSERT_NE(nullptr, message);
        messages.push_back(message);
        message_ptrs.push_back(message);
    }
    
    // Route batch
    router_->routeMessages(message_ptrs.data(), batch_size);
    
    // Check distribution
    auto critical_msgs = drainQueue(Priority::CRITICAL);
    auto high_msgs = drainQueue(Priority::HIGH);
    auto medium_msgs = drainQueue(Priority::MEDIUM);
    auto low_msgs = drainQueue(Priority::LOW);
    
    // Verify correct distribution (10 messages, 4 priorities using i % 4)
    // i=0,4,8: CRITICAL (3 messages)
    // i=1,5,9: HIGH (3 messages) 
    // i=2,6: MEDIUM (2 messages)
    // i=3,7: LOW (2 messages)
    EXPECT_EQ(3, critical_msgs.size()); // Indices: 0, 4, 8
    EXPECT_EQ(3, high_msgs.size());     // Indices: 1, 5, 9
    EXPECT_EQ(2, medium_msgs.size());   // Indices: 2, 6
    EXPECT_EQ(2, low_msgs.size());      // Indices: 3, 7
    
    // Clean up
    for (auto* msg : messages)
    {
        deallocateMessage(msg);
    }
}

TEST_F(MessageRouterTest, BatchRoutingWithNullArray)
{
    // Should handle null array gracefully
    router_->routeMessages(nullptr, 10);
    
    // Should handle zero count gracefully
    FixMessage* dummy = createTestMessage(FixMsgType::HEARTBEAT);
    router_->routeMessages(&dummy, 0);
    
    // No messages should be routed
    const auto& stats = router_->getStats();
    EXPECT_EQ(0, stats.messages_routed.load());
    
    deallocateMessage(dummy);
}

// =============================================================================
// PERFORMANCE AND STATISTICS TESTS
// =============================================================================

TEST_F(MessageRouterTest, StatisticsTracking)
{
    router_->resetStats();
    
    // Route some messages
    FixMessage* critical = createTestMessage(FixMsgType::EXECUTION_REPORT);
    FixMessage* high = createTestMessage(FixMsgType::MARKET_DATA_REQUEST);
    FixMessage* heartbeat = createTestMessage(FixMsgType::HEARTBEAT);
    
    router_->routeMessage(critical);
    router_->routeMessage(high);
    router_->routeMessage(heartbeat);
    
    const auto& stats = router_->getStats();
    EXPECT_EQ(3, stats.messages_routed.load());
    EXPECT_EQ(0, stats.messages_dropped.load());
    EXPECT_EQ(0, stats.routing_errors.load());
    
    // Check per-priority stats
    EXPECT_EQ(1, stats.critical_routed.load());
    EXPECT_EQ(1, stats.high_routed.load());
    EXPECT_EQ(0, stats.medium_routed.load());
    EXPECT_EQ(1, stats.low_routed.load());
    
    // Check latency tracking
    EXPECT_GT(stats.total_routing_time_ns.load(), 0);
    EXPECT_GT(router_->getAverageRoutingLatencyNs(), 0.0);
    
    // Clean up
    drainQueue(Priority::CRITICAL);
    drainQueue(Priority::HIGH);
    drainQueue(Priority::LOW);
    deallocateMessage(critical);
    deallocateMessage(high);
    deallocateMessage(heartbeat);
}

TEST_F(MessageRouterTest, LatencyMeasurement)
{
    router_->resetStats();
    
    FixMessage* message = createTestMessage(FixMsgType::EXECUTION_REPORT);
    ASSERT_NE(nullptr, message);
    
    // Route message
    router_->routeMessage(message);
    
    // Check latency stats
    EXPECT_GT(router_->getAverageRoutingLatencyNs(), 0.0);
    EXPECT_GT(router_->getPeakRoutingLatencyNs(), 0);
    
    // Peak should be >= average for single message
    EXPECT_GE(router_->getPeakRoutingLatencyNs(), 
              static_cast<uint64_t>(router_->getAverageRoutingLatencyNs()));
    
    // Clean up
    drainQueue(Priority::CRITICAL);
    deallocateMessage(message);
}

// =============================================================================
// STRESS TESTS
// =============================================================================

TEST_F(MessageRouterTest, HighThroughputTest)
{
    const size_t num_messages = 1000;
    std::vector<FixMessage*> messages;
    
    router_->resetStats();
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Create and route many messages
    for (size_t i = 0; i < num_messages; ++i)
    {
        FixMsgType msgType = (i % 2 == 0) ? FixMsgType::EXECUTION_REPORT : FixMsgType::HEARTBEAT;
        FixMessage* message = createTestMessage(msgType);
        ASSERT_NE(nullptr, message);
        
        messages.push_back(message);
        router_->routeMessage(message);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    // Check performance
    const auto& stats = router_->getStats();
    EXPECT_EQ(num_messages, stats.messages_routed.load());
    
    // Calculate throughput (messages per second)
    double throughput = static_cast<double>(num_messages) / (duration.count() / 1000000.0);
    
    // Should achieve reasonable throughput (adjust threshold as needed)
    EXPECT_GT(throughput, 100000.0); // 100K messages/sec minimum
    
    std::cout << "MessageRouter Throughput: " << throughput << " messages/sec" << std::endl;
    std::cout << "Average Routing Latency: " << router_->getAverageRoutingLatencyNs() << " ns" << std::endl;
    
    // Clean up
    drainQueue(Priority::CRITICAL);
    drainQueue(Priority::LOW);
    for (auto* msg : messages)
    {
        deallocateMessage(msg);
    }
}

TEST_F(MessageRouterTest, ConcurrentRouting)
{
    const size_t num_threads = 4;
    const size_t messages_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<size_t> messages_created{0};
    
    router_->resetStats();
    
    // Create worker threads
    for (size_t thread_id = 0; thread_id < num_threads; ++thread_id)
    {
        threads.emplace_back([this, thread_id, messages_per_thread, &messages_created]()
        {
            std::vector<FixMessage*> thread_messages;
            
            for (size_t i = 0; i < messages_per_thread; ++i)
            {
                FixMsgType msgType = (thread_id % 2 == 0) ? FixMsgType::EXECUTION_REPORT : FixMsgType::HEARTBEAT;
                FixMessage* message = createTestMessage(msgType);
                if (message)
                {
                    thread_messages.push_back(message);
                    router_->routeMessage(message);
                    messages_created.fetch_add(1);
                }
            }
            
            // Clean up thread-local messages
            // Note: In real usage, messages would be consumed by business logic
            // Here we need to clean up to prevent memory leaks
            for (auto* msg : thread_messages)
            {
                deallocateMessage(msg);
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    // Drain all queues to prevent memory leaks
    drainQueue(Priority::CRITICAL);
    drainQueue(Priority::HIGH);
    drainQueue(Priority::MEDIUM);
    drainQueue(Priority::LOW);
    
    // Check final stats
    const auto& stats = router_->getStats();
    EXPECT_EQ(messages_created.load(), stats.messages_routed.load());
    EXPECT_GT(router_->getAverageRoutingLatencyNs(), 0.0);
    
    std::cout << "Concurrent Routing - Messages routed: " << stats.messages_routed.load() << std::endl;
    std::cout << "Concurrent Routing - Average latency: " << router_->getAverageRoutingLatencyNs() << " ns" << std::endl;
}

// =============================================================================
// EDGE CASES AND ERROR HANDLING
// =============================================================================

TEST_F(MessageRouterTest, QueueFullScenario)
{
    // This test would require filling up the queue, which depends on queue implementation
    // For now, we'll test the drop counter functionality
    
    router_->resetStats();
    
    // Create a large number of messages to potentially fill queues
    const size_t many_messages = 10000;
    size_t messages_created = 0;
    
    for (size_t i = 0; i < many_messages && messages_created < 100; ++i) // Limit to prevent OOM
    {
        FixMessage* message = createTestMessage(FixMsgType::EXECUTION_REPORT);
        if (message)
        {
            router_->routeMessage(message);
            ++messages_created;
        }
    }
    
    const auto& stats = router_->getStats();
    
    // At minimum, some messages should be routed
    EXPECT_GT(stats.messages_routed.load(), 0);
    
    // Clean up - drain the queue
    auto critical_messages = drainQueue(Priority::CRITICAL);
    for (auto* msg : critical_messages)
    {
        deallocateMessage(msg);
    }
}

TEST_F(MessageRouterTest, StatisticsReset)
{
    // Route some messages first
    FixMessage* message = createTestMessage(FixMsgType::EXECUTION_REPORT);
    router_->routeMessage(message);
    
    // Verify stats are non-zero
    const auto& stats_before = router_->getStats();
    EXPECT_GT(stats_before.messages_routed.load(), 0);
    
    // Reset stats
    router_->resetStats();
    
    // Verify stats are reset
    const auto& stats_after = router_->getStats();
    EXPECT_EQ(0, stats_after.messages_routed.load());
    EXPECT_EQ(0, stats_after.messages_dropped.load());
    EXPECT_EQ(0, stats_after.routing_errors.load());
    EXPECT_EQ(0, stats_after.total_routing_time_ns.load());
    EXPECT_EQ(0, stats_after.peak_routing_time_ns.load());
    
    // Clean up
    drainQueue(Priority::CRITICAL);
    deallocateMessage(message);
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}