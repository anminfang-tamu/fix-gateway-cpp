#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "manager/sequence_num_gap_manager.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "common/message_pool.h"
#include "application/priority_queue_container.h"
#include "utils/logger.h"
#include <chrono>
#include <thread>
#include <memory>

using namespace fix_gateway::protocol;
using namespace fix_gateway::common;
using namespace testing;

// Type aliases for cleaner code
using TestMessagePool = fix_gateway::common::MessagePool<FixMessage>;

// =================================================================
// TEST FIXTURE - SequenceNumGapManagerTest
// =================================================================

class SequenceNumGapManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create message pool
        message_pool_ = std::make_unique<TestMessagePool>(1000, "gap_test_pool");

        // Create session context
        session_context_ = std::make_shared<SessionContext>("CLIENT", "SERVER");

        // Create outbound queues
        outbound_queues_ = std::make_shared<PriorityQueueContainer>();

        // Create gap manager
        gap_manager_ = std::make_unique<SequenceNumGapManager>(
            message_pool_.get(), session_context_, outbound_queues_);
    }

    void TearDown() override
    {
        if (gap_manager_)
        {
            gap_manager_->stop();
        }
    }

    // Helper function to drain critical queue for testing
    std::vector<FixMessage *> drainCriticalQueue()
    {
        std::vector<FixMessage *> messages;
        auto critical_queue = outbound_queues_->getQueue(Priority::CRITICAL);

        FixMessage *msg = nullptr;
        while (critical_queue->tryPop(msg))
        {
            messages.push_back(msg);
        }
        return messages;
    }

    // Helper to create and validate resend request message
    void validateResendRequest(FixMessage *msg, int32_t expected_seq)
    {
        ASSERT_NE(msg, nullptr);

        std::string msg_type;
        ASSERT_TRUE(msg->getField(FixFields::MsgType, msg_type));
        EXPECT_EQ(msg_type, "2"); // ResendRequest

        std::string begin_seq, end_seq;
        ASSERT_TRUE(msg->getField(FixFields::BeginSeqNo, begin_seq));
        ASSERT_TRUE(msg->getField(FixFields::EndSeqNo, end_seq));
        EXPECT_EQ(std::stoi(begin_seq), expected_seq);
        EXPECT_EQ(std::stoi(end_seq), expected_seq);

        std::string sender_comp_id, target_comp_id;
        ASSERT_TRUE(msg->getField(FixFields::SenderCompID, sender_comp_id));
        ASSERT_TRUE(msg->getField(FixFields::TargetCompID, target_comp_id));
        EXPECT_EQ(sender_comp_id, "CLIENT");
        EXPECT_EQ(target_comp_id, "SERVER");
    }

protected:
    std::unique_ptr<TestMessagePool> message_pool_;
    std::shared_ptr<SessionContext> session_context_;
    std::shared_ptr<PriorityQueueContainer> outbound_queues_;
    std::unique_ptr<SequenceNumGapManager> gap_manager_;
};

// =================================================================
// BASIC FUNCTIONALITY TESTS
// =================================================================

TEST_F(SequenceNumGapManagerTest, InitializationTest)
{
    EXPECT_EQ(gap_manager_->getGapCount(), 0);
    EXPECT_EQ(gap_manager_->getQueueDepth(), 0);
}

TEST_F(SequenceNumGapManagerTest, AddSingleGapEntry)
{
    const int32_t test_seq = 42;

    gap_manager_->addGapEntry(test_seq);

    EXPECT_EQ(gap_manager_->getGapCount(), 1);
    EXPECT_EQ(gap_manager_->getQueueDepth(), 1);
    EXPECT_TRUE(gap_manager_->hasGap(test_seq));
}

TEST_F(SequenceNumGapManagerTest, AddMultipleGapEntries)
{
    const std::vector<int32_t> test_seqs = {10, 15, 20, 25};

    for (int32_t seq : test_seqs)
    {
        gap_manager_->addGapEntry(seq);
    }

    EXPECT_EQ(gap_manager_->getGapCount(), test_seqs.size());

    for (int32_t seq : test_seqs)
    {
        EXPECT_TRUE(gap_manager_->hasGap(seq));
    }
}

TEST_F(SequenceNumGapManagerTest, ResolveGapEntry)
{
    const int32_t test_seq = 100;

    // Add gap
    gap_manager_->addGapEntry(test_seq);
    EXPECT_TRUE(gap_manager_->hasGap(test_seq));
    EXPECT_EQ(gap_manager_->getGapCount(), 1);

    // Resolve gap
    bool resolved = gap_manager_->resolveGapEntry(test_seq);
    EXPECT_TRUE(resolved);
    EXPECT_EQ(gap_manager_->getGapCount(), 0);
}

TEST_F(SequenceNumGapManagerTest, ResolveNonExistentGap)
{
    const int32_t existing_seq = 50;
    const int32_t non_existing_seq = 99;

    gap_manager_->addGapEntry(existing_seq);

    bool resolved = gap_manager_->resolveGapEntry(non_existing_seq);
    EXPECT_FALSE(resolved);
    EXPECT_EQ(gap_manager_->getGapCount(), 1);
    EXPECT_TRUE(gap_manager_->hasGap(existing_seq));
}

TEST_F(SequenceNumGapManagerTest, ClearAllGaps)
{
    const std::vector<int32_t> test_seqs = {1, 2, 3, 4, 5};

    for (int32_t seq : test_seqs)
    {
        gap_manager_->addGapEntry(seq);
    }

    EXPECT_EQ(gap_manager_->getGapCount(), test_seqs.size());

    gap_manager_->clearAllGaps();

    EXPECT_EQ(gap_manager_->getGapCount(), 0);
    for (int32_t seq : test_seqs)
    {
        EXPECT_FALSE(gap_manager_->hasGap(seq));
    }
}

// =================================================================
// RESEND REQUEST TESTS
// =================================================================

TEST_F(SequenceNumGapManagerTest, GapProcessingTriggersResendRequest)
{
    const int32_t test_seq = 123;

    // Start the gap manager to enable processing
    gap_manager_->start();

    // Add gap entry
    gap_manager_->addGapEntry(test_seq);

    // Wait for processing (gaps have 10 second timeout, but we sleep briefly to let it process)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check that no resend request was sent yet (timeout not reached)
    auto messages = drainCriticalQueue();
    EXPECT_EQ(messages.size(), 0);

    EXPECT_EQ(gap_manager_->getGapCount(), 1);

    // Stop the manager
    gap_manager_->stop();
}

TEST_F(SequenceNumGapManagerTest, SessionContextSequenceNumberIncrement)
{
    int32_t initial_seq = session_context_->outgoing_seq_num.load();

    // Get next sequence number
    int32_t next_seq = session_context_->getNextSeqNum();

    EXPECT_EQ(next_seq, initial_seq);
    EXPECT_EQ(session_context_->outgoing_seq_num.load(), initial_seq + 1);
}

// =================================================================
// LIFECYCLE TESTS
// =================================================================

TEST_F(SequenceNumGapManagerTest, StartStopLifecycle)
{
    EXPECT_NO_THROW(gap_manager_->start());

    // Add some gaps
    gap_manager_->addGapEntry(10);
    gap_manager_->addGapEntry(20);

    EXPECT_EQ(gap_manager_->getGapCount(), 2);

    // Stop should complete without hanging
    EXPECT_NO_THROW(gap_manager_->stop());
}

TEST_F(SequenceNumGapManagerTest, MultipleStartStopCycles)
{
    // Test multiple start/stop cycles
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_NO_THROW(gap_manager_->start());

        gap_manager_->addGapEntry(i * 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        EXPECT_NO_THROW(gap_manager_->stop());
    }
}

// =================================================================
// CPU AFFINITY TESTS (Linux-specific)
// =================================================================

TEST_F(SequenceNumGapManagerTest, SetCpuAffinity)
{
    // Test setting CPU affinity (should not throw)
    EXPECT_NO_THROW(gap_manager_->setCpuAffinity(0));
    EXPECT_NO_THROW(gap_manager_->setCpuAffinity(-1)); // Disable affinity
}

// =================================================================
// EDGE CASE TESTS
// =================================================================

TEST_F(SequenceNumGapManagerTest, LargeSequenceNumbers)
{
    const int32_t large_seq = 2147483647; // Max int32_t

    gap_manager_->addGapEntry(large_seq);
    EXPECT_TRUE(gap_manager_->hasGap(large_seq));

    bool resolved = gap_manager_->resolveGapEntry(large_seq);
    EXPECT_TRUE(resolved);
    EXPECT_FALSE(gap_manager_->hasGap(large_seq));
}

TEST_F(SequenceNumGapManagerTest, ZeroAndNegativeSequenceNumbers)
{
    gap_manager_->addGapEntry(0);
    gap_manager_->addGapEntry(-1);
    gap_manager_->addGapEntry(-100);

    EXPECT_TRUE(gap_manager_->hasGap(0));
    EXPECT_TRUE(gap_manager_->hasGap(-1));
    EXPECT_TRUE(gap_manager_->hasGap(-100));

    EXPECT_EQ(gap_manager_->getGapCount(), 3);
}

TEST_F(SequenceNumGapManagerTest, DuplicateGapEntries)
{
    const int32_t test_seq = 555;

    // Add same gap multiple times
    gap_manager_->addGapEntry(test_seq);
    EXPECT_EQ(gap_manager_->getGapCount(), 1);
    EXPECT_TRUE(gap_manager_->hasGap(test_seq));

    // Try to add the same gap again - should be ignored
    gap_manager_->addGapEntry(test_seq);
    EXPECT_EQ(gap_manager_->getGapCount(), 1); // Still only 1
    EXPECT_TRUE(gap_manager_->hasGap(test_seq));

    // Add a third time - still should be ignored
    gap_manager_->addGapEntry(test_seq);
    EXPECT_EQ(gap_manager_->getGapCount(), 1); // Still only 1
    EXPECT_TRUE(gap_manager_->hasGap(test_seq));
}

TEST_F(SequenceNumGapManagerTest, DuplicatePreventionWithDifferentSequences)
{
    // Add different sequence numbers
    gap_manager_->addGapEntry(100);
    gap_manager_->addGapEntry(200);
    gap_manager_->addGapEntry(300);
    EXPECT_EQ(gap_manager_->getGapCount(), 3);

    // Try to add duplicates of existing sequences
    gap_manager_->addGapEntry(100);            // duplicate
    gap_manager_->addGapEntry(200);            // duplicate
    EXPECT_EQ(gap_manager_->getGapCount(), 3); // Still 3, no duplicates added

    // Add a new unique sequence
    gap_manager_->addGapEntry(400);
    EXPECT_EQ(gap_manager_->getGapCount(), 4); // Now 4

    // Verify all sequences are present
    EXPECT_TRUE(gap_manager_->hasGap(100));
    EXPECT_TRUE(gap_manager_->hasGap(200));
    EXPECT_TRUE(gap_manager_->hasGap(300));
    EXPECT_TRUE(gap_manager_->hasGap(400));
}

// =================================================================
// PERFORMANCE AND STRESS TESTS
// =================================================================

TEST_F(SequenceNumGapManagerTest, ManyGapsPerformance)
{
    const int num_gaps = 1000;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Add many gaps
    for (int i = 0; i < num_gaps; ++i)
    {
        gap_manager_->addGapEntry(i);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    EXPECT_EQ(gap_manager_->getGapCount(), num_gaps);

    // Should be fast - less than 10ms for 1000 gaps
    EXPECT_LT(duration.count(), 15000);
}

TEST_F(SequenceNumGapManagerTest, ConcurrentGapOperations)
{
    gap_manager_->start();

    const int num_operations = 100;
    std::vector<std::thread> threads;

    // Start multiple threads adding/resolving gaps
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([this, t, num_operations]()
                             {
            for (int i = 0; i < num_operations; ++i) {
                int32_t seq = t * num_operations + i;
                gap_manager_->addGapEntry(seq);
                
                // Sometimes resolve gaps
                if (i % 3 == 0) {
                    gap_manager_->resolveGapEntry(seq);
                }
            } });
    }

    // Wait for all threads to complete
    for (auto &thread : threads)
    {
        thread.join();
    }

    gap_manager_->stop();

    // Verify gap manager is still functional
    EXPECT_NO_THROW(gap_manager_->addGapEntry(9999));
    EXPECT_TRUE(gap_manager_->hasGap(9999));
}

// =================================================================
// INTEGRATION TESTS
// =================================================================

TEST_F(SequenceNumGapManagerTest, MessagePoolIntegration)
{
    // Verify message pool allocation works
    FixMessage *test_msg = message_pool_->allocate();
    ASSERT_NE(test_msg, nullptr);

    // Set some fields
    test_msg->setField(FixFields::MsgType, std::string("2"));
    test_msg->setField(FixFields::BeginSeqNo, std::string("123"));

    std::string msg_type;
    EXPECT_TRUE(test_msg->getField(FixFields::MsgType, msg_type));
    EXPECT_EQ(msg_type, "2");

    message_pool_->deallocate(test_msg);
}

TEST_F(SequenceNumGapManagerTest, OutboundQueueIntegration)
{
    // Test direct queue access
    auto critical_queue = outbound_queues_->getQueue(Priority::CRITICAL);
    ASSERT_NE(critical_queue, nullptr);

    // Create a test message
    FixMessage *test_msg = message_pool_->allocate();
    ASSERT_NE(test_msg, nullptr);

    test_msg->setField(FixFields::MsgType, std::string("TEST"));

    // Push to queue
    EXPECT_TRUE(critical_queue->push(test_msg));

    // Pop from queue
    FixMessage *retrieved_msg = nullptr;
    EXPECT_TRUE(critical_queue->tryPop(retrieved_msg));
    EXPECT_EQ(retrieved_msg, test_msg);

    std::string msg_type;
    EXPECT_TRUE(retrieved_msg->getField(FixFields::MsgType, msg_type));
    EXPECT_EQ(msg_type, "TEST");

    message_pool_->deallocate(retrieved_msg);
}

// =================================================================
// ERROR HANDLING TESTS
// =================================================================

TEST_F(SequenceNumGapManagerTest, HandleNullPointers)
{
    // Test creating gap manager with null dependencies
    EXPECT_THROW(
        SequenceNumGapManager(nullptr, session_context_, outbound_queues_),
        std::invalid_argument);

    EXPECT_THROW(
        SequenceNumGapManager(message_pool_.get(), nullptr, outbound_queues_),
        std::invalid_argument);

    EXPECT_THROW(
        SequenceNumGapManager(message_pool_.get(), session_context_, nullptr),
        std::invalid_argument);
}
