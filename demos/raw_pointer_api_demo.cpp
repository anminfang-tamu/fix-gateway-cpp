#include "common/message_pool.h"
#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "utils/logger.h"
#include <iostream>

using namespace fix_gateway;
using namespace fix_gateway::common;
using namespace fix_gateway::protocol;

/**
 * @brief Clean Raw Pointer API Demo
 *
 * Shows the simplified, performance-optimized FixMessage API
 * using only raw pointers with MessagePool for maximum speed.
 */
int main()
{
    std::cout << "\n🎯 FixMessage Raw Pointer API Demo" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << "Pure raw pointer interface - no shared_ptr overhead!\n"
              << std::endl;

    try
    {
        // Create a message pool for FixMessage objects
        MessagePool<FixMessage> messagePool(1000, "fix_message_pool");
        messagePool.prewarm();

        std::cout << "📊 Pool Status: " << messagePool.capacity() << " total capacity, "
                  << messagePool.available() << " available\n"
                  << std::endl;

        // Session management messages
        std::cout << "🔐 Session Management:" << std::endl;

        FixMessage *logon = FixMessage::createLogon(messagePool,
                                                    "SENDER1", "TARGET1",
                                                    30, 0);
        if (logon)
        {
            std::cout << "   ✅ Logon: " << logon->getMsgType() << std::endl;
            std::cout << "   📋 Fields: " << logon->getFieldCount() << std::endl;
        }

        FixMessage *heartbeat = FixMessage::createHeartbeat(messagePool,
                                                            "SENDER1", "TARGET1");
        if (heartbeat)
        {
            std::cout << "   ✅ Heartbeat: " << heartbeat->getMsgType() << std::endl;
        }

        // Trading messages
        std::cout << "\n📈 Trading Messages:" << std::endl;

        FixMessage *newOrder = FixMessage::createNewOrderSingle(
            messagePool,
            "ORDER_001", // ClOrdID
            "AAPL",      // Symbol
            "1",         // Side (Buy)
            "100",       // OrderQty
            "150.50",    // Price
            "2",         // OrdType (Limit)
            "0"          // TimeInForce (Day)
        );

        if (newOrder)
        {
            std::cout << "   ✅ New Order Single: " << newOrder->getClOrdID() << std::endl;
            std::cout << "   📊 Symbol: " << newOrder->getSymbol() << std::endl;
            std::cout << "   💰 Price: " << newOrder->getPrice() << std::endl;
            std::cout << "   📦 Qty: " << newOrder->getOrderQty() << std::endl;
        }

        FixMessage *cancelOrder = FixMessage::createOrderCancelRequest(
            messagePool,
            "ORDER_001",  // OrigClOrdID
            "CANCEL_001", // ClOrdID
            "AAPL",       // Symbol
            "1"           // Side
        );

        if (cancelOrder)
        {
            std::cout << "   ✅ Order Cancel: " << cancelOrder->getClOrdID() << std::endl;
        }

        // Ultra-fast pattern using FastFixPatterns
        std::cout << "\n⚡ Ultra-Fast Pattern:" << std::endl;

        FixMessage *fastOrder = FastFixPatterns::createFastOrder(
            messagePool,
            "FAST_001", // ClOrdID
            "TSLA",     // Symbol
            "2",        // Side (Sell)
            "200",      // Qty
            "250.75"    // Price
        );

        if (fastOrder)
        {
            std::cout << "   ✅ Fast Order: " << fastOrder->getClOrdID() << std::endl;
            std::cout << "   🚀 Symbol: " << fastOrder->getSymbol() << std::endl;
        }

        // Show serialization
        std::cout << "\n📤 Message Serialization:" << std::endl;
        if (newOrder)
        {
            newOrder->updateLengthAndChecksum();
            std::cout << "   Raw FIX: " << newOrder->toString().substr(0, 80) << "..." << std::endl;
        }

        // Pool management - clean up
        std::cout << "\n🧹 Memory Management:" << std::endl;
        std::cout << "   Pool available before cleanup: " << messagePool.available() << std::endl;

        if (logon)
            messagePool.deallocate(logon);
        if (heartbeat)
            messagePool.deallocate(heartbeat);
        if (newOrder)
            messagePool.deallocate(newOrder);
        if (cancelOrder)
            messagePool.deallocate(cancelOrder);
        if (fastOrder)
            messagePool.deallocate(fastOrder);

        std::cout << "   Pool available after cleanup: " << messagePool.available() << std::endl;

        std::cout << "\n🎉 Demo Complete!" << std::endl;
        std::cout << "\n💡 Key Benefits:" << std::endl;
        std::cout << "   ✅ No shared_ptr overhead" << std::endl;
        std::cout << "   ✅ Pre-allocated pool (predictable latency)" << std::endl;
        std::cout << "   ✅ Manual memory management (full control)" << std::endl;
        std::cout << "   ✅ Optimal for high-frequency trading" << std::endl;
        std::cout << "   ✅ Clean, simple API" << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Demo failed: " + std::string(e.what()));
        return 1;
    }
}