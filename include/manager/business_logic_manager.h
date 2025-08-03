#pragma once

#include "inbound_message_manager.h"
#include "application/order_book_interface.h"
#include "protocol/fix_message.h"

#include <unordered_map>
#include <string>
#include <memory>

namespace fix_gateway::manager
{
    using OrderBookInterface = fix_gateway::application::OrderBookInterface;

    /**
     * @brief Handles business logic and trading-related FIX messages
     *
     * Processes order management messages (NEW_ORDER_SINGLE, ORDER_CANCEL_REQUEST, etc.)
     * and execution reports. Integrates with order book and applies business rules.
     */
    class BusinessLogicManager : public InboundMessageManager
    {
    public:
        struct OrderState
        {
            std::string order_id;
            std::string client_order_id;
            std::string symbol;
            double quantity;
            double price;
            char side;         // '1' = Buy, '2' = Sell
            char order_status; // '0' = New, '1' = Partial Fill, '2' = Filled, etc.
            std::chrono::steady_clock::time_point creation_time;
        };

        struct BusinessStats
        {
            uint64_t orders_received = 0;
            uint64_t orders_accepted = 0;
            uint64_t orders_rejected = 0;
            uint64_t cancels_received = 0;
            uint64_t cancels_executed = 0;
            uint64_t execution_reports_sent = 0;

            // Risk metrics
            uint64_t risk_checks_passed = 0;
            uint64_t risk_checks_failed = 0;

            // Performance metrics
            double avg_order_processing_time_ns = 0.0;
            double avg_cancel_processing_time_ns = 0.0;
        };

    public:
        explicit BusinessLogicManager();
        ~BusinessLogicManager() override = default;

        // Order book integration
        void setOrderBook(std::shared_ptr<OrderBookInterface> order_book);

        // Business rule configuration
        void setMaxOrderSize(double max_size) { max_order_size_ = max_size; }
        void setMaxOrdersPerSecond(int max_rate) { max_orders_per_second_ = max_rate; }
        void enableRiskChecks(bool enable) { risk_checks_enabled_ = enable; }

        // Extended stats
        BusinessStats getBusinessStats() const { return business_stats_; }

        // Order state queries
        bool getOrderState(const std::string &order_id, OrderState &state) const;
        std::vector<OrderState> getActiveOrders() const;

    protected:
        // Implementation of abstract methods from parent
        bool canHandleMessage(const FixMessage *message) const override;
        bool handleMessage(FixMessage *message) override;
        std::vector<FixMsgType> getSupportedMessageTypes() const override;

    private:
        // Order processing methods
        bool handleNewOrderSingle(FixMessage *message);
        bool handleOrderCancelRequest(FixMessage *message);
        bool handleOrderCancelReplaceRequest(FixMessage *message);
        bool handleOrderStatusRequest(FixMessage *message);

        // Response generation
        bool sendExecutionReport(const OrderState &order, char exec_type, char order_status);
        bool sendOrderCancelReject(const std::string &client_order_id, const std::string &reason);

        // Business logic validation
        bool validateNewOrder(const FixMessage *message, std::string &reject_reason);
        bool applyRiskChecks(const FixMessage *message, std::string &reject_reason);
        bool checkOrderSize(double size, std::string &reject_reason);
        bool checkOrderRate(std::string &reject_reason);

        // Order state management
        void storeOrderState(const OrderState &order);
        bool findOrderByClientId(const std::string &client_order_id, OrderState &order);
        void updateOrderState(const std::string &order_id, char new_status);

        // Utility methods
        std::string generateOrderId();
        double extractPrice(const FixMessage *message);
        double extractQuantity(const FixMessage *message);
        char extractSide(const FixMessage *message);
        std::string extractSymbol(const FixMessage *message);

    private:
        // Order book interface
        std::shared_ptr<OrderBookInterface> order_book_;

        // Order state storage
        std::unordered_map<std::string, OrderState> active_orders_by_id_;
        std::unordered_map<std::string, std::string> client_id_to_order_id_;

        // Business configuration
        double max_order_size_ = 1000000.0; // Default 1M
        int max_orders_per_second_ = 1000;  // Default 1K orders/sec
        bool risk_checks_enabled_ = true;

        // Rate limiting
        std::chrono::steady_clock::time_point last_order_time_;
        int orders_this_second_ = 0;

        // Statistics
        mutable BusinessStats business_stats_;

        // Order ID generation
        std::atomic<uint64_t> order_id_counter_{1};
    };

} // namespace fix_gateway::manager