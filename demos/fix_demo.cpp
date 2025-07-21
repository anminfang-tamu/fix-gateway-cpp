#include "common/message_pool.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "utils/performance_timer.h"
#include "utils/logger.h"
#include <iostream>
#include <iomanip>

using namespace fix_gateway;
using namespace fix_gateway::protocol;

class FixEngineDemo
{
public:
    void run()
    {
        std::cout << "\n=== FIX 4.4 Engine Demonstration ===" << std::endl;
        std::cout << "Showing FIX protocol implementation on high-performance foundation\n"
                  << std::endl;

        // Test 1: Session Management
        testSessionMessages();

        // Test 2: Trading Messages
        testTradingMessages();

        // Test 3: Message Validation
        testMessageValidation();

        // Test 4: Performance Testing
        testFIXPerformance();

        std::cout << "=== FIX Engine Demo Complete ===\n"
                  << std::endl;
    }

private:
    void testSessionMessages()
    {
        std::cout << "📋 Test 1: FIX Session Management" << std::endl;

        // Create Logon message
        FixMessage *logon = FixMessage::createLogon(messagePool_, "HEDGE_FUND", "EXCHANGE", 30);
        logon->setMsgSeqNum(1);

        std::string logonStr = logon->toString();
        std::cout << "✅ Logon Message Created:" << std::endl;
        std::cout << "   Raw: " << logonStr << std::endl;
        std::cout << "   Summary: " << logon->getFieldsSummary() << std::endl;

        // Create Heartbeat
        FixMessage *heartbeat = FixMessage::createHeartbeat(messagePool_, "HEDGE_FUND", "EXCHANGE");
        heartbeat->setMsgSeqNum(2);

        std::cout << "✅ Heartbeat Message:" << std::endl;
        std::cout << "   Summary: " << heartbeat->getFieldsSummary() << std::endl;

        // Clean up
        messagePool_.deallocate(logon);
        messagePool_.deallocate(heartbeat);

        std::cout << std::endl;
    }

    void testTradingMessages()
    {
        std::cout << "📋 Test 2: Trading Messages (New Order Single)" << std::endl;

        // Create New Order Single - Buy 1000 AAPL at $150.50
        auto orderMsg = std::make_shared<FixMessage>();
        orderMsg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
        orderMsg->setSenderCompID("HEDGE_FUND");
        orderMsg->setTargetCompID("EXCHANGE");
        orderMsg->setMsgSeqNum(3);

        // Order details
        orderMsg->setField(FixFields::ClOrdID, "ORDER_001");
        orderMsg->setField(FixFields::Symbol, "AAPL");
        orderMsg->setField(FixFields::Side, OrderSide::Buy);
        orderMsg->setField(FixFields::OrderQty, "1000");
        orderMsg->setField(FixFields::Price, 150.50, 2);
        orderMsg->setField(FixFields::OrdType, OrderType::Limit);
        orderMsg->setField(FixFields::TimeInForce, TimeInForce::Day);

        std::cout << "✅ New Order Single Created:" << std::endl;
        std::cout << "   Summary: " << orderMsg->getFieldsSummary() << std::endl;
        std::cout << "   Valid: " << (orderMsg->isValid() ? "YES" : "NO") << std::endl;

        // Create Execution Report (Fill)
        auto execReport = std::make_shared<FixMessage>();
        execReport->setField(FixFields::MsgType, MsgTypes::ExecutionReport);
        execReport->setSenderCompID("EXCHANGE");
        execReport->setTargetCompID("HEDGE_FUND");
        execReport->setMsgSeqNum(1);

        // Execution details
        execReport->setField(FixFields::OrderID, "EXCH_12345");
        execReport->setField(FixFields::ExecID, "FILL_001");
        execReport->setField(FixFields::ExecType, ExecType::Fill);
        execReport->setField(FixFields::OrdStatus, OrderStatus::Filled);
        execReport->setField(FixFields::Symbol, "AAPL");
        execReport->setField(FixFields::Side, OrderSide::Buy);
        execReport->setField(FixFields::OrderQty, "1000");
        execReport->setField(FixFields::LastQty, "1000");
        execReport->setField(FixFields::LastPx, 150.48, 2);
        execReport->setField(FixFields::LeavesQty, "0");
        execReport->setField(FixFields::CumQty, "1000");
        execReport->setField(FixFields::AvgPx, 150.48, 2);

        std::cout << "✅ Execution Report (Fill):" << std::endl;
        std::cout << "   Summary: " << execReport->getFieldsSummary() << std::endl;
        std::cout << "   Message Type: " << (execReport->isApplicationMessage() ? "Application" : "Session") << std::endl;

        std::cout << std::endl;
    }

    void testMessageValidation()
    {
        std::cout << "📋 Test 3: Message Validation" << std::endl;

        // Test valid message
        auto validMsg = std::make_shared<FixMessage>();
        validMsg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
        validMsg->setSenderCompID("SENDER");
        validMsg->setTargetCompID("TARGET");
        validMsg->setField(FixFields::ClOrdID, "VALID_001");
        validMsg->setField(FixFields::Symbol, "TSLA");
        validMsg->setField(FixFields::Side, OrderSide::Sell);
        validMsg->setField(FixFields::OrderQty, "500");

        auto validationErrors = validMsg->validate();
        std::cout << "✅ Valid Order Validation: " << validationErrors.size() << " errors" << std::endl;

        // Test invalid message
        auto invalidMsg = std::make_shared<FixMessage>();
        invalidMsg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
        // Missing required fields

        auto invalidErrors = invalidMsg->validate();
        std::cout << "❌ Invalid Order Validation: " << invalidErrors.size() << " errors:" << std::endl;
        for (const auto &error : invalidErrors)
        {
            std::cout << "   - " << error << std::endl;
        }

        std::cout << std::endl;
    }

    void testFIXPerformance()
    {
        std::cout << "📋 Test 4: FIX Engine Performance" << std::endl;

        const int MESSAGE_COUNT = 10000;

        // Test message creation performance
        auto startTime = utils::PerformanceTimer::now();
        for (int i = 0; i < MESSAGE_COUNT; ++i)
        {
            auto msg = std::make_shared<FixMessage>();
            msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
            msg->setSenderCompID("HEDGE_FUND");
            msg->setTargetCompID("EXCHANGE");
            msg->setMsgSeqNum(i + 1);
            msg->setField(FixFields::ClOrdID, "ORDER_" + std::to_string(i));
            msg->setField(FixFields::Symbol, "AAPL");
            msg->setField(FixFields::Side, OrderSide::Buy);
            msg->setField(FixFields::OrderQty, "100");
            msg->setField(FixFields::Price, 150.0 + (i % 100) * 0.01, 2);
        }
        auto endTime = utils::PerformanceTimer::now();

        auto createTime = utils::PerformanceTimer::duration(startTime, endTime);
        std::cout << "✅ Message Creation Performance:" << std::endl;
        std::cout << "   " << MESSAGE_COUNT << " messages created in "
                  << utils::PerformanceTimer::toMilliseconds(createTime) << " ms" << std::endl;
        std::cout << "   Average: " << createTime.count() / MESSAGE_COUNT
                  << " ns per message" << std::endl;

        // Test serialization performance
        auto testMsg = std::make_shared<FixMessage>();
        testMsg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
        testMsg->setSenderCompID("HEDGE_FUND");
        testMsg->setTargetCompID("EXCHANGE");
        testMsg->setMsgSeqNum(1);
        testMsg->setField(FixFields::ClOrdID, "PERF_TEST");
        testMsg->setField(FixFields::Symbol, "AAPL");
        testMsg->setField(FixFields::Side, OrderSide::Buy);
        testMsg->setField(FixFields::OrderQty, "1000");
        testMsg->setField(FixFields::Price, 150.50, 2);

        auto serializeStartTime = utils::PerformanceTimer::now();
        for (int i = 0; i < MESSAGE_COUNT; ++i)
        {
            std::string serialized = testMsg->toString();
            (void)serialized; // Prevent optimization
        }
        auto serializeEndTime = utils::PerformanceTimer::now();

        auto serializeTime = utils::PerformanceTimer::duration(serializeStartTime, serializeEndTime);
        std::cout << "✅ Serialization Performance:" << std::endl;
        std::cout << "   " << MESSAGE_COUNT << " messages serialized in "
                  << utils::PerformanceTimer::toMilliseconds(serializeTime) << " ms" << std::endl;
        std::cout << "   Average: " << serializeTime.count() / MESSAGE_COUNT
                  << " ns per serialization" << std::endl;

        // Show sample serialized message
        std::string sample = testMsg->toString();
        std::cout << "✅ Sample FIX Message (raw):" << std::endl;
        std::cout << "   " << sample << std::endl;

        std::cout << "✅ Sample FIX Message (formatted):" << std::endl;
        std::cout << testMsg->toFormattedString() << std::endl;

        std::cout << std::endl;
    }

private:
    // Message pool for all FixMessage allocations
    FixMessagePool messagePool_{1000, "fix_demo_pool"};
};

int main()
{
    LOG_INFO("=== FIX Engine on High-Performance Foundation ===");

    try
    {
        FixEngineDemo demo;
        demo.run();

        std::cout << "🎉 FIX Engine Successfully Demonstrated!" << std::endl;
        std::cout << "Ready for integration with:" << std::endl;
        std::cout << "  - High-performance message queues (42ns latency)" << std::endl;
        std::cout << "  - Core-pinned threads (Linux deployment)" << std::endl;
        std::cout << "  - Lock-free data structures" << std::endl;
        std::cout << "  - TCP connections and session management" << std::endl;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception in FIX demo: " + std::string(e.what()));
        return 1;
    }

    return 0;
}