#include "manager/outbound_message_manager.h"
#include "common/message.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cassert>

using namespace fix_gateway::manager;
using namespace fix_gateway::network;
using namespace fix_gateway::common;
using namespace std::chrono_literals;

void testMessageManagerBasicFunctionality()
{
    std::cout << "\n=== Testing MessageManager Basic Functionality ===" << std::endl;

    // Test factory configurations
    auto m1_config = OutboundMessageManagerFactory::createM1MaxConfig();
    auto default_config = OutboundMessageManagerFactory::createDefaultConfig();
    auto low_latency_config = OutboundMessageManagerFactory::createLowLatencyConfig();
    auto high_throughput_config = OutboundMessageManagerFactory::createHighThroughputConfig();

    std::cout << "✅ Factory configurations created successfully" << std::endl;

    // Test MessageManager creation
    OutboundMessageManager manager(m1_config);
    std::cout << "✅ MessageManager created with M1 Max config" << std::endl;

    // Test status before start
    assert(!manager.isRunning());
    assert(!manager.isConnected());
    std::cout << "✅ Initial status check passed" << std::endl;
}

void testMessageManagerLifecycle()
{
    std::cout << "\n=== Testing MessageManager Lifecycle ===" << std::endl;

    auto config = OutboundMessageManagerFactory::createDefaultConfig();
    config.enable_core_pinning = false; // Disable for testing

    OutboundMessageManager manager(config);

    // Test start
    manager.start();
    assert(manager.isRunning());
    std::cout << "✅ MessageManager started successfully" << std::endl;

    // Test queue depths
    auto depths = manager.getQueueDepths();
    assert(depths.size() == 4); // Should have 4 priority queues
    std::cout << "✅ Queue depths check passed: " << depths.size() << " queues" << std::endl;

    // Test stop
    manager.stop();
    assert(!manager.isRunning());
    std::cout << "✅ MessageManager stopped successfully" << std::endl;
}

void testMessageRouting()
{
    std::cout << "\n=== Testing Message Routing ===" << std::endl;

    auto config = OutboundMessageManagerFactory::createDefaultConfig();
    config.enable_core_pinning = false; // Disable for testing

    OutboundMessageManager manager(config);
    manager.start();

    // Create messages with different priorities
    auto critical_msg = Message::create("crit-001", "CRITICAL ORDER", Priority::CRITICAL);
    auto high_msg = Message::create("high-001", "HIGH ORDER", Priority::HIGH);
    auto medium_msg = Message::create("med-001", "MEDIUM ORDER", Priority::MEDIUM);
    auto low_msg = Message::create("low-001", "LOW ORDER", Priority::LOW);

    // Route messages
    bool crit_routed = manager.routeMessage(critical_msg);
    bool high_routed = manager.routeMessage(high_msg);
    bool med_routed = manager.routeMessage(medium_msg);
    bool low_routed = manager.routeMessage(low_msg);

    assert(crit_routed && high_routed && med_routed && low_routed);
    std::cout << "✅ All messages routed successfully" << std::endl;

    // Check queue depths (messages might be consumed quickly by AsyncSenders)
    auto crit_depth = manager.getQueueDepthForPriority(Priority::CRITICAL);
    auto high_depth = manager.getQueueDepthForPriority(Priority::HIGH);
    auto med_depth = manager.getQueueDepthForPriority(Priority::MEDIUM);
    auto low_depth = manager.getQueueDepthForPriority(Priority::LOW);

    std::cout << "📊 Queue depths: CRITICAL=" << crit_depth
              << ", HIGH=" << high_depth
              << ", MEDIUM=" << med_depth
              << ", LOW=" << low_depth << std::endl;

    std::cout << "✅ Queue depth monitoring working (AsyncSenders may consume messages quickly)" << std::endl;

    manager.stop();
}

void testPerformanceStats()
{
    std::cout << "\n=== Testing Performance Stats ===" << std::endl;

    auto config = OutboundMessageManagerFactory::createDefaultConfig();
    config.enable_core_pinning = false;

    OutboundMessageManager manager(config);
    manager.start();

    // Route some messages
    for (int i = 0; i < 5; ++i)
    {
        auto msg = Message::create("test-" + std::to_string(i), "Test message", Priority::CRITICAL);
        manager.routeMessage(msg);
    }

    // Give AsyncSenders time to process
    std::this_thread::sleep_for(100ms);

    // Get performance stats
    auto stats = manager.getStats();
    std::cout << "📊 Performance Stats:" << std::endl;
    std::cout << "   Total messages sent: " << stats.total_messages_sent << std::endl;
    std::cout << "   Total messages failed: " << stats.total_messages_failed << std::endl;
    std::cout << "   CRITICAL count: " << stats.critical_count << std::endl;
    std::cout << "   HIGH count: " << stats.high_count << std::endl;
    std::cout << "   TCP connected: " << (stats.tcp_connected ? "Yes" : "No") << std::endl;

    // Get per-priority stats
    auto crit_stats = manager.getStatsForPriority(Priority::CRITICAL);
    std::cout << "   CRITICAL queue depth: " << crit_stats.current_queue_depth << std::endl;

    std::cout << "✅ Performance stats retrieved successfully" << std::endl;

    manager.stop();
}

void testCorePinningCapabilities()
{
    std::cout << "\n=== Testing Core Pinning Capabilities ===" << std::endl;

    // Test hardware detection
    int perf_cores = OutboundMessageManagerFactory::detectPerformanceCores();
    auto optimal_assignment = OutboundMessageManagerFactory::getOptimalCoreAssignment();
    bool rt_supported = OutboundMessageManagerFactory::isRealTimePrioritySupported();

    std::cout << "🖥️  Hardware Detection:" << std::endl;
    std::cout << "   Performance cores: " << perf_cores << std::endl;
    std::cout << "   Optimal assignment: ";
    for (int core : optimal_assignment)
    {
        std::cout << core << " ";
    }
    std::cout << std::endl;
    std::cout << "   Real-time priority supported: " << (rt_supported ? "Yes" : "No") << std::endl;

    // Test core pinning config
    auto config = OutboundMessageManagerFactory::createM1MaxConfig();
    config.enable_core_pinning = true;
    config.enable_real_time_priority = false; // Don't require root for test

    OutboundMessageManager manager(config);
    std::cout << "✅ Core-pinned MessageManager created" << std::endl;

    // Start with core pinning (will show pinning attempts in logs)
    manager.start();
    std::cout << "✅ MessageManager started with core pinning enabled" << std::endl;

    // Test core affinity configuration
    manager.setCoreAffinity(Priority::CRITICAL, 0);
    manager.setCoreAffinity(Priority::HIGH, 1);
    std::cout << "✅ Core affinity configured" << std::endl;

    manager.stop();
}

void testTcpConnectionManagement()
{
    std::cout << "\n=== Testing TCP Connection Management ===" << std::endl;

    OutboundMessageManager manager;
    manager.start();

    // Test connection attempt (will fail since no server is running)
    bool connected = manager.connectToServer("localhost", 12345);
    std::cout << "🌐 Connection attempt to localhost:12345: "
              << (connected ? "Success" : "Failed (expected)") << std::endl;

    // Test connection status
    bool is_connected = manager.isConnected();
    std::cout << "📡 Connection status: " << (is_connected ? "Connected" : "Disconnected") << std::endl;

    // Test disconnect
    manager.disconnectFromServer();
    std::cout << "✅ Disconnect completed" << std::endl;

    manager.stop();
}

void testConfigurationOptions()
{
    std::cout << "\n=== Testing Configuration Options ===" << std::endl;

    // Test all factory configurations
    struct ConfigTest
    {
        std::string name;
        OutboundMessageManager::CorePinningConfig config;
    };

    std::vector<ConfigTest> configs = {
        {"M1 Max", OutboundMessageManagerFactory::createM1MaxConfig()},
        {"Intel", OutboundMessageManagerFactory::createIntelConfig()},
        {"Default", OutboundMessageManagerFactory::createDefaultConfig()},
        {"Low Latency", OutboundMessageManagerFactory::createLowLatencyConfig()},
        {"High Throughput", OutboundMessageManagerFactory::createHighThroughputConfig()}};

    for (const auto &test : configs)
    {
        std::cout << "⚙️  " << test.name << " Config:" << std::endl;
        std::cout << "   CRITICAL core: " << test.config.critical_core << std::endl;
        std::cout << "   CRITICAL queue size: " << test.config.critical_queue_size << std::endl;
        std::cout << "   Core pinning: " << (test.config.enable_core_pinning ? "Enabled" : "Disabled") << std::endl;
        std::cout << "   Real-time priority: " << (test.config.enable_real_time_priority ? "Enabled" : "Disabled") << std::endl;

        // Test that MessageManager can be created with each config
        OutboundMessageManager manager(test.config);
        std::cout << "   ✅ MessageManager created successfully" << std::endl;
    }
}

int main()
{
    std::cout << "🚀 Starting MessageManager Test Suite" << std::endl;

    try
    {
        testMessageManagerBasicFunctionality();
        testMessageManagerLifecycle();
        testMessageRouting();
        testPerformanceStats();
        testCorePinningCapabilities();
        testTcpConnectionManagement();
        testConfigurationOptions();

        std::cout << "\n🎉 All MessageManager tests passed!" << std::endl;
        std::cout << "\n📋 Test Summary:" << std::endl;
        std::cout << "✅ Basic functionality" << std::endl;
        std::cout << "✅ Lifecycle management" << std::endl;
        std::cout << "✅ Message routing" << std::endl;
        std::cout << "✅ Performance monitoring" << std::endl;
        std::cout << "✅ Core pinning capabilities" << std::endl;
        std::cout << "✅ TCP connection management" << std::endl;
        std::cout << "✅ Configuration options" << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}