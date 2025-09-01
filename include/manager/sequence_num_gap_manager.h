#pragma once

#include <cstdint>
#include <atomic>
#include <thread>
#include <chrono>
#include "utils/lockfree_queue.h"
#include "common/message_pool.h"
#include "common/message.h"
#include "application/priority_queue_container.h"

using FixMessage = fix_gateway::protocol::FixMessage;
using MessagePool = fix_gateway::common::MessagePool<FixMessage>;

struct GapQueueEntry
{
    int32_t seq_num;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::steady_clock::time_point timeout_deadline;
    int32_t retry_count{0};
    bool is_resolved{false};

    GapQueueEntry() = default;
    GapQueueEntry(int32_t seq, std::chrono::milliseconds timeout_ms)
        : seq_num(seq), timestamp(std::chrono::steady_clock::now()),
          timeout_deadline(timestamp + timeout_ms), retry_count(0) {}
};

struct SessionContext
{
    std::string sender_comp_id;
    std::string target_comp_id;
    std::atomic<int32_t> outgoing_seq_num{1};

    SessionContext(const std::string &sender, const std::string &target) : sender_comp_id(sender), target_comp_id(target) {}

    int32_t getNextSeqNum() { return outgoing_seq_num.fetch_add(1); }
};

class SequenceNumGapManager
{
public:
    SequenceNumGapManager(
        std::shared_ptr<MessagePool> message_pool,
        std::shared_ptr<SessionContext> session_context,
        std::shared_ptr<PriorityQueueContainer> outbound_queues);

    // lifecycle
    void start();
    void stop();

    // loop
    void loop();

    // gap management
    void addGapEntry(int32_t seq_num);
    bool resolveGapEntry(int32_t seq_num);
    void escalateGapEntry(int32_t seq_num);
    bool hasGap(int32_t seq_num);
    size_t getGapCount() const;
    void clearAllGaps();

    // monitoring
    size_t getQueueDepth() const;
    void setCpuAffinity(int cpu_core);

private:
    // thread management
    std::atomic<bool> is_running_{false};
    std::thread gap_manager_thread_;
    int cpu_core_{-1};

    // configuration constants
    static constexpr int32_t kGapQueueSize = 1024;
    static constexpr int32_t kGapTimeoutMs = 10000; // 10 seconds
    static constexpr int32_t kMaxRetryCount = 5;
    static constexpr int32_t kPollingIntervalMs = 1;
    static constexpr size_t kWarningThreshold = 50;
    static constexpr size_t kCriticalThreshold = 200;

    // gap tracking
    fix_gateway::utils::LockFreeQueue<GapQueueEntry> gap_queue_{kGapQueueSize, "gap_queue"};

    // message pool (inject from existing)
    std::shared_ptr<MessagePool> message_pool_;
    std::shared_ptr<SessionContext> session_context_;
    std::shared_ptr<PriorityQueueContainer> outbound_queues_;

    // internal methods
    void processGaps();
    void handleTimeout(const GapQueueEntry &entry);
    void sendResendRequest(int32_t seq_num);
    bool isTimeout(const GapQueueEntry &entry) const;

    // Logging helpers
    void logInfo(const std::string &message) const;
    void logError(const std::string &message) const;
    void logWarning(const std::string &message) const;
    void logDebug(const std::string &message) const;
};