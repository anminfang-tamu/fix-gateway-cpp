#define _GNU_SOURCE
#include "manager/sequence_num_gap_manager.h"
#include "config/priority_config.h"
#include "utils/logger.h"
#include <sched.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <cstring> // for strerror
#endif

SequenceNumGapManager::SequenceNumGapManager(
    std::shared_ptr<MessagePool> message_pool,
    std::shared_ptr<SessionContext> session_context,
    std::shared_ptr<PriorityQueueContainer> outbound_queues)
    : message_pool_(message_pool),
      session_context_(session_context),
      outbound_queues_(outbound_queues),
      gap_queue_(kGapQueueSize, "gap_queue")
{
    if (!message_pool_ || !session_context_ || !outbound_queues_)
    {
        throw std::invalid_argument("SequenceNumGapManager: All dependencies must be provided during initialization");
    }
}

void SequenceNumGapManager::logInfo(const std::string &message) const
{
    fix_gateway::utils::Logger::getInstance().info("[SequenceNumGapManager] " + message);
}

void SequenceNumGapManager::logError(const std::string &message) const
{
    fix_gateway::utils::Logger::getInstance().error("[SequenceNumGapManager] " + message);
}

void SequenceNumGapManager::logWarning(const std::string &message) const
{
    fix_gateway::utils::Logger::getInstance().warn("[SequenceNumGapManager] " + message);
}

void SequenceNumGapManager::logDebug(const std::string &message) const
{
    fix_gateway::utils::Logger::getInstance().debug("[SequenceNumGapManager] " + message);
}

void SequenceNumGapManager::start()
{
    is_running_.store(true);
    gap_manager_thread_ = std::thread(&SequenceNumGapManager::loop, this);
}

void SequenceNumGapManager::stop()
{
    is_running_.store(false);
    gap_manager_thread_.join();
}

void SequenceNumGapManager::loop()
{
    if (cpu_core_ != -1)
    {
        setCpuAffinity(cpu_core_);
    }
    while (is_running_.load())
    {
        processGaps();
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollingIntervalMs));
    }
}

void SequenceNumGapManager::addGapEntry(int32_t seq_num)
{
    gap_queue_.push(GapQueueEntry(seq_num, std::chrono::milliseconds(kGapTimeoutMs)));
}

bool SequenceNumGapManager::resolveGapEntry(int32_t seq_num)
{
    bool is_resolved = false;
    std::vector<GapQueueEntry> temp_entries;
    while (true)
    {
        GapQueueEntry entry;
        if (gap_queue_.tryPop(entry))
        {
            if (entry.seq_num == seq_num && !entry.is_resolved)
            {
                entry.is_resolved = true;
                is_resolved = true;
                break;
            }
            else
            {
                temp_entries.push_back(entry);
            }
        }
        else
        {
            break;
        }
    }

    for (const auto &entry : temp_entries)
    {
        gap_queue_.push(entry);
    }

    return is_resolved;
}

bool SequenceNumGapManager::hasGap(int32_t seq_num)
{
    bool has_gap = false;
    std::vector<GapQueueEntry> temp_entries;
    while (true)
    {
        GapQueueEntry entry;
        if (gap_queue_.tryPop(entry))
        {
            if (entry.seq_num == seq_num && !entry.is_resolved)
            {
                has_gap = true;
                temp_entries.push_back(entry);
            }
        }
        else
        {
            break;
        }
    }

    for (const auto &entry : temp_entries)
    {
        gap_queue_.push(entry);
    }

    return has_gap;
}

void SequenceNumGapManager::escalateGapEntry(int32_t seq_num)
{
    while (true)
    {
        GapQueueEntry entry;
        if (!gap_queue_.tryPop(entry))
        {
            break;
        }
        if (entry.seq_num == seq_num)
        {
            entry.retry_count++;
            gap_queue_.push(entry);
            break;
        }
    }
}

size_t SequenceNumGapManager::getGapCount() const
{
    return gap_queue_.size();
}

void SequenceNumGapManager::clearAllGaps()
{
    while (!gap_queue_.empty())
    {
        GapQueueEntry entry;
        gap_queue_.tryPop(entry);
    }
}

size_t SequenceNumGapManager::getQueueDepth() const
{
    return gap_queue_.size();
}

void SequenceNumGapManager::setCpuAffinity(int cpu_core)
{
    cpu_core_ = cpu_core;
    if (cpu_core_ != -1)
    {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_core_, &cpuset);

        int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (result != 0)
        {
            std::cerr << "[SequenceNumGapManager] Failed to set thread affinity to core " << cpu_core_ << " - error: " << result << " (" << strerror(result) << ")" << std::endl;
        }
        else
        {
            std::cout << "[SequenceNumGapManager] Successfully set thread affinity to core " << cpu_core_ << std::endl;
        }
#else
        std::cout << "[SequenceNumGapManager] Thread pinning not supported for this platform" << std::endl;
#endif
    }
}

void SequenceNumGapManager::processGaps()
{
    std::vector<GapQueueEntry> entries_to_requeue;

    while (true)
    {
        GapQueueEntry entry;
        if (gap_queue_.tryPop(entry))
        {
            if (entry.is_resolved)
            {
                continue;
            }
            if (isTimeout(entry))
            {
                if (entry.retry_count < kMaxRetryCount)
                {
                    sendResendRequest(entry.seq_num);
                    entry.retry_count++;
                    entry.timeout_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kGapTimeoutMs);
                    entries_to_requeue.push_back(entry);
                }
                else
                {
                    handleTimeout(entry); // max retries exceeded
                }
            }
            else
            {
                entries_to_requeue.push_back(entry); // Not timeout yet, keep waiting
            }
        }
        else
        {
            break;
        }
    }

    for (const auto &entry : entries_to_requeue)
    {
        gap_queue_.push(entry);
    }
}

void SequenceNumGapManager::handleTimeout(const GapQueueEntry &entry)
{
    // TODO: handle timeout
    // Log critical error - this is a serious FIX protocol violation
    logError("CRITICAL: Sequence gap timeout after " + std::to_string(entry.retry_count) +
             " retries for seq " + std::to_string(entry.seq_num) +
             ", gap age: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - entry.timestamp).count()) + "ms");

    // Mark as permanently missing and continue operations
    logWarning("Marking sequence " + std::to_string(entry.seq_num) + " as permanently missing");

    // Log for audit/monitoring purposes
    logInfo("Gap entry seq=" + std::to_string(entry.seq_num) + " marked as timeout, continuing operations");
}

void SequenceNumGapManager::sendResendRequest(int32_t seq_num)
{
    FixMessage *msg = message_pool_->allocate();
    if (!msg)
    {
        ("[SequenceNumGapManager] Failed to allocate message for ResendRequest seq=" + std::to_string(seq_num) + " from pool");
        return;
    }

    try
    {
        // Build ResendRequest message (MsgType = "2")
        msg->setField(fix_gateway::protocol::FixFields::MsgType, std::string(fix_gateway::protocol::MsgTypes::ResendRequest));
        msg->setField(fix_gateway::protocol::FixFields::BeginSeqNo, static_cast<int>(seq_num));
        msg->setField(fix_gateway::protocol::FixFields::EndSeqNo, static_cast<int>(seq_num));

        // Set session fields
        msg->setField(fix_gateway::protocol::FixFields::SenderCompID,
                      session_context_->sender_comp_id); // 49
        msg->setField(fix_gateway::protocol::FixFields::TargetCompID,
                      session_context_->target_comp_id); // 56
        msg->setField(fix_gateway::protocol::FixFields::MsgSeqNum,
                      session_context_->getNextSeqNum()); // 34
        msg->setSendingTime();                            // Current time (field 52)

        // Update length and checksum (required for valid FIX message)
        msg->updateLengthAndChecksum();

        // Route to CRITICAL priority queue (session-critical message)
        auto critical_queue = outbound_queues_->getQueue(Priority::CRITICAL);
        if (!critical_queue->push(msg))
        {
            // Queue is full, cleanup and log error
            message_pool_->deallocate(msg);
            logError("[SequenceNumGapManager] CRITICAL queue full, failed to queue ResendRequest for seq " + std::to_string(seq_num));
            return;
        }

        logInfo("[SequenceNumGapManager] Sent ResendRequest for sequence " + std::to_string(seq_num) + " to CRITICAL queue");
    }
    catch (const std::exception &e)
    {
        message_pool_->deallocate(msg);
        logError("[SequenceNumGapManager] Failed to send ResendRequest seq=" + std::to_string(seq_num) + " - " + e.what());
    }
}

bool SequenceNumGapManager::isTimeout(const GapQueueEntry &entry) const
{
    return std::chrono::steady_clock::now() > entry.timeout_deadline;
}
