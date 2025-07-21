#include "protocol/fix_message.h"
#include "protocol/fix_fields.h"
#include "utils/performance_timer.h"
#include <iostream>
#include <memory>

using namespace fix_gateway::protocol;

void demonstratePerformanceGain()
{
    constexpr int ITERATIONS = 10000;

    std::cout << "=== Quick Performance Comparison ===" << std::endl;
    std::cout << "Testing " << ITERATIONS << " FIX message operations\n"
              << std::endl;

    // Test 1: Current shared_ptr approach
    {
        std::cout << "📊 Current shared_ptr approach:" << std::endl;
        auto startTime = fix_gateway::utils::PerformanceTimer::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            auto msg = std::make_shared<FixMessage>();
            msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
            msg->setField(FixFields::ClOrdID, "ORDER_" + std::to_string(i));
            msg->setField(FixFields::Symbol, "AAPL");

            // Simulate routing through system
            auto copy1 = msg; // MessageManager
            auto copy2 = msg; // Priority Queue
            auto copy3 = msg; // AsyncSender

            // Serialize (simulate sending)
            volatile auto serialized = copy3->toString();
            (void)serialized;
        }

        auto endTime = fix_gateway::utils::PerformanceTimer::now();
        auto duration = fix_gateway::utils::PerformanceTimer::duration(startTime, endTime);
        std::cout << "   Time: " << fix_gateway::utils::PerformanceTimer::toMilliseconds(duration) << " ms" << std::endl;
        std::cout << "   Per message: " << duration.count() / ITERATIONS << " ns" << std::endl;
    }

    // Test 2: Optimized unique_ptr approach
    {
        std::cout << "\n📊 Optimized unique_ptr approach:" << std::endl;
        auto startTime = fix_gateway::utils::PerformanceTimer::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            auto msg = std::make_unique<FixMessage>();
            msg->setField(FixFields::MsgType, MsgTypes::NewOrderSingle);
            msg->setField(FixFields::ClOrdID, "ORDER_" + std::to_string(i));
            msg->setField(FixFields::Symbol, "AAPL");

            // Move-only semantics through system
            auto moved1 = std::move(msg);    // To MessageManager
            auto moved2 = std::move(moved1); // To Priority Queue
            auto moved3 = std::move(moved2); // To AsyncSender

            // Serialize (simulate sending)
            volatile auto serialized = moved3->toString();
            (void)serialized;
        }

        auto endTime = fix_gateway::utils::PerformanceTimer::now();
        auto duration = fix_gateway::utils::PerformanceTimer::duration(startTime, endTime);
        std::cout << "   Time: " << fix_gateway::utils::PerformanceTimer::toMilliseconds(duration) << " ms" << std::endl;
        std::cout << "   Per message: " << duration.count() / ITERATIONS << " ns" << std::endl;
    }
}

int main()
{
    std::cout << "🚀 Quick Performance Demo: shared_ptr vs unique_ptr" << std::endl;
    std::cout << "Simulating your FIX message processing pipeline\n"
              << std::endl;

    demonstratePerformanceGain();

    std::cout << "\n💡 Key Takeaway:" << std::endl;
    std::cout << "   • unique_ptr eliminates atomic reference counting overhead" << std::endl;
    std::cout << "   • Perfect for single-ownership scenarios in your pipeline" << std::endl;
    std::cout << "   • Combined with your 42ns queues = sub-100ns messaging!" << std::endl;

    return 0;
}