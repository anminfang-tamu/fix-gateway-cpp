#include "application/fix_gateway.h"
#include "protocol/fix_message.h"
#include "utils/logger.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace fix_gateway::application;
using namespace fix_gateway::protocol;

// Example trading logic that processes FIX messages
class TradingSystem
{
public:
    // Handle incoming FIX messages from the parser
    void handleFixMessage(FixMessage *message)
    {
        if (!message)
        {
            std::cerr << "Null message received!" << std::endl;
            return;
        }

        std::string msgType = message->getMsgType();
        std::cout << "\n=== Received FIX Message ===" << std::endl;
        std::cout << "Message Type: " << msgType << std::endl;
        std::cout << "Summary: " << message->getFieldsSummary() << std::endl;

        // Handle different message types
        if (msgType == "D") // NewOrderSingle
        {
            handleNewOrder(message);
        }
        else if (msgType == "8") // ExecutionReport
        {
            handleExecutionReport(message);
        }
        else if (msgType == "0") // Heartbeat
        {
            handleHeartbeat(message);
        }
        else
        {
            std::cout << "Unknown message type: " << msgType << std::endl;
        }
    }

    // Handle error conditions
    void handleError(const std::string &error)
    {
        std::cerr << "ERROR: " << error << std::endl;
    }

private:
    void handleNewOrder(FixMessage *message)
    {
        std::cout << "Processing New Order:" << std::endl;
        std::cout << "  ClOrdID: " << message->getClOrdID() << std::endl;
        std::cout << "  Symbol: " << message->getSymbol() << std::endl;
        std::cout << "  Side: " << message->getSide() << std::endl;
        std::cout << "  Quantity: " << message->getOrderQty() << std::endl;
        std::cout << "  Price: " << message->getPrice() << std::endl;

        // TODO: Add your order processing logic here
        // - Validate order parameters
        // - Check risk limits
        // - Route to execution engine
        // - Generate execution reports
    }

    void handleExecutionReport(FixMessage *message)
    {
        std::cout << "Processing Execution Report:" << std::endl;
        std::cout << "  ClOrdID: " << message->getClOrdID() << std::endl;
        std::cout << "  Symbol: " << message->getSymbol() << std::endl;

        // TODO: Add your execution processing logic here
        // - Update order status
        // - Calculate positions
        // - Send confirmations
    }

    void handleHeartbeat(FixMessage *message)
    {
        std::cout << "Received Heartbeat - connection alive" << std::endl;
        // Heartbeats are handled automatically by the session layer
    }
};

int main()
{
    std::cout << "FIX Gateway Integration Example" << std::endl;
    std::cout << "===============================" << std::endl;

    try
    {
        // =================================================================
        // STEP 1: Create the integrated FIX gateway
        // =================================================================

        FixGateway gateway(8192); // 8K message pool
        TradingSystem trading_system;

        // =================================================================
        // STEP 2: Setup callbacks for message processing
        // =================================================================

        // Set message callback - this is where your trading logic goes!
        gateway.setMessageCallback(
            [&trading_system](FixMessage *message)
            {
                trading_system.handleFixMessage(message);
            });

        // Set error callback
        gateway.setErrorCallback(
            [&trading_system](const std::string &error)
            {
                trading_system.handleError(error);
            });

        // =================================================================
        // STEP 3: Configure parser for optimal performance
        // =================================================================

        gateway.setMaxMessageSize(16384);  // 16KB max message size
        gateway.setValidateChecksum(true); // Enable checksum validation
        gateway.setStrictValidation(true); // Enable strict FIX validation

        // =================================================================
        // STEP 4: Connect to FIX server
        // =================================================================

        std::string host = "localhost"; // Replace with your FIX server
        int port = 8080;                // Replace with your FIX port

        std::cout << "\nConnecting to FIX server..." << std::endl;

        if (!gateway.connect(host, port))
        {
            std::cerr << "Failed to connect to FIX server!" << std::endl;
            return 1;
        }

        std::cout << "Connected successfully!" << std::endl;

        // =================================================================
        // STEP 5: Send a test message (optional)
        // =================================================================

        // Example: Send a heartbeat
        std::string heartbeat = "8=FIX.4.4\0019=49\00135=0\00149=CLIENT\00156=SERVER\00134=1\00152=20231215-10:30:00\00110=123\001";
        gateway.sendRawMessage(heartbeat);

        // =================================================================
        // STEP 6: Run the message processing loop
        // =================================================================

        std::cout << "\nListening for FIX messages..." << std::endl;
        std::cout << "Press Ctrl+C to exit" << std::endl;

        // Keep the application running to receive messages
        while (gateway.isConnected())
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // Print performance statistics every 10 seconds
            static int counter = 0;
            if (++counter % 10 == 0)
            {
                auto parser_stats = gateway.getParserStats();
                auto pool_stats = gateway.getPoolStats();

                std::cout << "\n=== Performance Statistics ===" << std::endl;
                std::cout << "Messages parsed: " << parser_stats.messages_parsed << std::endl;
                std::cout << "Parse errors: " << parser_stats.parse_errors << std::endl;
                std::cout << "Average parse time: " << parser_stats.getAverageParseTimeNs() << " ns" << std::endl;
                std::cout << "Pool allocations: " << pool_stats.total_allocations << std::endl;
                std::cout << "Pool capacity: " << pool_stats.total_capacity << std::endl;
                std::cout << "Pool available: " << pool_stats.available_count << std::endl;
            }
        }

        gateway.disconnect();
        std::cout << "Disconnected from FIX server" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

// =================================================================
// EXAMPLE OUTPUT:
// =================================================================
/*
FIX Gateway Integration Example
===============================

Connecting to FIX server...
Connected successfully!

Listening for FIX messages...
Press Ctrl+C to exit

=== Received FIX Message ===
Message Type: D
Summary: MsgType=D ClOrdID=ORDER123 Symbol=AAPL Side=1 MsgSeqNum=1

Processing New Order:
  ClOrdID: ORDER123
  Symbol: AAPL
  Side: 1
  Quantity: 100
  Price: 150.50

=== Performance Statistics ===
Messages parsed: 1247
Parse errors: 0
Average parse time: 87 ns      ← Zero-copy parsing performance!
Pool allocations: 1247
Pool capacity: 8192
Pool available: 8191

*/