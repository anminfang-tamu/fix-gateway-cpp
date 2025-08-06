#pragma once

#include "protocol/fix_message.h"

namespace fix_gateway::application
{
    using FixMessage = fix_gateway::protocol::FixMessage;

    class OrderBookInterface
    {
    public:
        virtual ~OrderBookInterface() = default;

        virtual void addOrder(const FixMessage &order) = 0;
        virtual void cancelOrder(const FixMessage &order) = 0;
        virtual void updateOrder(const FixMessage &order) = 0;
    };
}