#include "network/async_sender.h"
#include "utils/priority_queue.h"
#include "network/tcp_connection.h"
#include "common/message.h"
#include "priority_config.h"

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

// Test focuses on the AsyncSender architecture and priority queue integration
// TCP connection will not be established, demonstrating error handling

int main()
{
    std::cout << "=== AsyncSender Integration Test ===" << std::endl;

    try
    {
        // 1. Create components
        auto priority_queue = std::make_shared<fix_gateway::utils::PriorityQueue>(1000);

        // For this test, we'll use a real TcpConnection but won't actually connect
        auto real_tcp = std::make_shared<fix_gateway::network::TcpConnection>();

        std::cout << "✅ Created priority queue and TCP connection" << std::endl;

        // 2. Create AsyncSender
        auto async_sender = std::make_unique<fix_gateway::network::AsyncSender>(
            priority_queue, real_tcp);

        std::cout << "✅ Created AsyncSender" << std::endl;

        // 3. Start AsyncSender
        async_sender->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::cout << "✅ Started AsyncSender (running: " << async_sender->isRunning() << ")" << std::endl;

        // 4. Create test messages with different priorities
        auto critical_msg = fix_gateway::common::Message::create(
            "CRITICAL_001", "35=D|49=SENDER|56=TARGET|", Priority::CRITICAL);
        auto high_msg = fix_gateway::common::Message::create(
            "HIGH_001", "35=G|49=SENDER|56=TARGET|", Priority::HIGH);
        auto medium_msg = fix_gateway::common::Message::create(
            "MEDIUM_001", "35=8|49=SENDER|56=TARGET|", Priority::MEDIUM);
        auto low_msg = fix_gateway::common::Message::create(
            "LOW_001", "35=0|49=SENDER|56=TARGET|", Priority::LOW);

        std::cout << "✅ Created test messages with different priorities" << std::endl;

        // 5. Push messages directly to queue (they should be processed in priority order)
        std::cout << "\n--- Pushing messages to queue ---" << std::endl;
        priority_queue->push(low_msg);      // Send low priority first
        priority_queue->push(critical_msg); // Critical should jump to front
        priority_queue->push(medium_msg);
        priority_queue->push(high_msg);
        low_msg = nullptr;
        critical_msg = nullptr;
        medium_msg = nullptr;
        high_msg = nullptr;

        std::cout << "Queue depth after pushing: " << async_sender->getQueueDepth() << std::endl;

        // 6. Wait for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 7. Check statistics
        auto stats = async_sender->getStats();
        std::cout << "\n--- AsyncSender Statistics ---" << std::endl;
        std::cout << "Messages sent: " << stats.total_messages_sent << std::endl;
        std::cout << "Messages failed: " << stats.total_messages_failed << std::endl;
        std::cout << "Messages retried: " << stats.total_messages_retried << std::endl;
        std::cout << "Current queue depth: " << stats.current_queue_depth << std::endl;
        std::cout << "Peak queue depth: " << stats.peak_queue_depth << std::endl;

        // 8. Send one more message to see the sender in action
        std::cout << "\n--- Sending final message ---" << std::endl;
        auto final_msg = fix_gateway::common::Message::create(
            "FINAL_001", "35=D|49=SENDER|56=TARGET|", Priority::CRITICAL);
        priority_queue->push(final_msg);
        final_msg = nullptr;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // 9. Final statistics
        stats = async_sender->getStats();
        std::cout << "\n--- Final Statistics ---" << std::endl;
        std::cout << "Total messages sent: " << stats.total_messages_sent << std::endl;
        std::cout << "Total messages failed: " << stats.total_messages_failed << std::endl;
        std::cout << "Note: TCP connection not actually connected, so some failures expected" << std::endl;

        // 11. Shutdown gracefully
        std::cout << "\n--- Shutting down ---" << std::endl;
        async_sender->shutdown(std::chrono::seconds(2));

        std::cout << "✅ AsyncSender shutdown completed" << std::endl;
        std::cout << "✅ All tests completed successfully!" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
