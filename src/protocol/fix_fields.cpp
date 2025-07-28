#include "protocol/fix_fields.h"
#include <string>
#include <cstring>

namespace fix_gateway::protocol
{
    namespace FixMsgTypeUtils
    {
        FixMsgType fromString(const char *msgTypeStr)
        {
            if (!msgTypeStr)
                return static_cast<FixMsgType>(-1); // Invalid

            // Compare with known message type strings
            if (strcmp(msgTypeStr, MsgTypes::Heartbeat) == 0)
                return FixMsgType::HEARTBEAT;
            else if (strcmp(msgTypeStr, MsgTypes::TestRequest) == 0)
                return FixMsgType::TEST_REQUEST;
            else if (strcmp(msgTypeStr, MsgTypes::Logon) == 0)
                return FixMsgType::LOGON;
            else if (strcmp(msgTypeStr, MsgTypes::Logout) == 0)
                return FixMsgType::LOGOUT;
            else if (strcmp(msgTypeStr, MsgTypes::NewOrderSingle) == 0)
                return FixMsgType::NEW_ORDER_SINGLE;
            else if (strcmp(msgTypeStr, MsgTypes::OrderCancelRequest) == 0)
                return FixMsgType::ORDER_CANCEL_REQUEST;
            else if (strcmp(msgTypeStr, MsgTypes::ExecutionReport) == 0)
                return FixMsgType::EXECUTION_REPORT;

            return static_cast<FixMsgType>(-1); // Unknown/unsupported message type
        }
    }
} // namespace fix_gateway::protocol