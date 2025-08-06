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
                return FixMsgType::UNKNOWN;

            // Ultra-fast single/double character comparisons (no strlen needed)
            // Session messages
            if (msgTypeStr[0] == '0' && msgTypeStr[1] == '\0')
                return FixMsgType::HEARTBEAT;
            if (msgTypeStr[0] == '1' && msgTypeStr[1] == '\0')
                return FixMsgType::TEST_REQUEST;
            if (msgTypeStr[0] == '2' && msgTypeStr[1] == '\0')
                return FixMsgType::RESEND_REQUEST;
            if (msgTypeStr[0] == '3' && msgTypeStr[1] == '\0')
                return FixMsgType::REJECT;
            if (msgTypeStr[0] == '4' && msgTypeStr[1] == '\0')
                return FixMsgType::SEQUENCE_RESET;
            if (msgTypeStr[0] == '5' && msgTypeStr[1] == '\0')
                return FixMsgType::LOGOUT;
            if (msgTypeStr[0] == 'A' && msgTypeStr[1] == '\0')
                return FixMsgType::LOGON;

            // Business messages
            if (msgTypeStr[0] == '8' && msgTypeStr[1] == '\0')
                return FixMsgType::EXECUTION_REPORT;
            if (msgTypeStr[0] == '9' && msgTypeStr[1] == '\0')
                return FixMsgType::ORDER_CANCEL_REJECT;
            if (msgTypeStr[0] == 'D' && msgTypeStr[1] == '\0')
                return FixMsgType::NEW_ORDER_SINGLE;
            if (msgTypeStr[0] == 'F' && msgTypeStr[1] == '\0')
                return FixMsgType::ORDER_CANCEL_REQUEST;
            if (msgTypeStr[0] == 'G' && msgTypeStr[1] == '\0')
                return FixMsgType::ORDER_CANCEL_REPLACE_REQUEST;
            if (msgTypeStr[0] == 'H' && msgTypeStr[1] == '\0')
                return FixMsgType::ORDER_STATUS_REQUEST;

            // Market data messages
            if (msgTypeStr[0] == 'V' && msgTypeStr[1] == '\0')
                return FixMsgType::MARKET_DATA_REQUEST;
            if (msgTypeStr[0] == 'W' && msgTypeStr[1] == '\0')
                return FixMsgType::MARKET_DATA_SNAPSHOT;
            if (msgTypeStr[0] == 'X' && msgTypeStr[1] == '\0')
                return FixMsgType::MARKET_DATA_INCREMENTAL_REFRESH;
            if (msgTypeStr[0] == 'Y' && msgTypeStr[1] == '\0')
                return FixMsgType::MARKET_DATA_REQUEST_REJECT;

            return FixMsgType::UNKNOWN;
        }
    }
} // namespace fix_gateway::protocol