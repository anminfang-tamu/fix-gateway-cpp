#pragma once

#include <string>
#include <unordered_map>

namespace fix_gateway::protocol
{
    // FIX Protocol Constants
    constexpr char FIX_SOH = '\001'; // Start of Header delimiter (ASCII 1)
    constexpr const char *FIX_VERSION_44 = "FIX.4.4";

    // Core FIX 4.4 Field Tags (most important for trading)
    namespace FixFields
    {
        // Session-Level Fields
        constexpr int BeginString = 8;       // FIX version
        constexpr int BodyLength = 9;        // Message length
        constexpr int CheckSum = 10;         // Message checksum
        constexpr int MsgType = 35;          // Message type
        constexpr int MsgSeqNum = 34;        // Message sequence number
        constexpr int SenderCompID = 49;     // Sender ID
        constexpr int TargetCompID = 56;     // Target ID
        constexpr int SendingTime = 52;      // Message timestamp
        constexpr int PossDupFlag = 43;      // Possible duplicate
        constexpr int PossResend = 97;       // Possible resend
        constexpr int OrigSendingTime = 122; // Original sending time

        // Session Management
        constexpr int EncryptMethod = 98;    // Encryption method
        constexpr int HeartBtInt = 108;      // Heartbeat interval
        constexpr int TestReqID = 112;       // Test request ID
        constexpr int ResetSeqNumFlag = 141; // Reset sequence numbers

        // Order Management Fields
        constexpr int ClOrdID = 11;      // Client order ID
        constexpr int OrderID = 37;      // Exchange order ID
        constexpr int OrigClOrdID = 41;  // Original client order ID
        constexpr int ExecID = 17;       // Execution ID
        constexpr int ExecType = 150;    // Execution type
        constexpr int OrdStatus = 39;    // Order status
        constexpr int Symbol = 55;       // Instrument symbol
        constexpr int Side = 54;         // Buy/Sell side
        constexpr int OrderQty = 38;     // Order quantity
        constexpr int Price = 44;        // Order price
        constexpr int OrdType = 40;      // Order type
        constexpr int TimeInForce = 59;  // Time in force
        constexpr int LastQty = 32;      // Last fill quantity
        constexpr int LastPx = 31;       // Last fill price
        constexpr int LeavesQty = 151;   // Remaining quantity
        constexpr int CumQty = 14;       // Cumulative quantity
        constexpr int AvgPx = 6;         // Average price
        constexpr int TransactTime = 60; // Transaction time
        constexpr int Text = 58;         // Free text

        // Market Data Fields
        constexpr int MDReqID = 262;                 // Market data request ID
        constexpr int SubscriptionRequestType = 263; // Subscribe/Unsubscribe
        constexpr int MarketDepth = 264;             // Market depth
        constexpr int MDUpdateType = 265;            // Update type
        constexpr int NoMDEntries = 268;             // Number of MD entries
        constexpr int MDEntryType = 269;             // Entry type (bid/offer/trade)
        constexpr int MDEntryPx = 270;               // Entry price
        constexpr int MDEntrySize = 271;             // Entry size
        constexpr int MDEntryTime = 273;             // Entry time

        // Risk and Position Fields
        constexpr int Account = 1;     // Account
        constexpr int Currency = 15;   // Currency
        constexpr int Commission = 12; // Commission
        constexpr int CommType = 13;   // Commission type
    }

    // FIX Message Types (MsgType field values)
    namespace MsgTypes
    {
        // Administrative Messages
        constexpr const char *Heartbeat = "0";
        constexpr const char *TestRequest = "1";
        constexpr const char *ResendRequest = "2";
        constexpr const char *Reject = "3";
        constexpr const char *SequenceReset = "4";
        constexpr const char *Logout = "5";
        constexpr const char *IndicationOfInterest = "6";
        constexpr const char *Advertisement = "7";
        constexpr const char *ExecutionReport = "8";
        constexpr const char *OrderCancelReject = "9";
        constexpr const char *Logon = "A";

        // Order Management Messages
        constexpr const char *NewOrderSingle = "D";
        constexpr const char *OrderCancelRequest = "F";
        constexpr const char *OrderCancelReplaceRequest = "G";
        constexpr const char *OrderStatusRequest = "H";

        // Market Data Messages
        constexpr const char *MarketDataRequest = "V";
        constexpr const char *MarketDataSnapshot = "W";
        constexpr const char *MarketDataIncrementalRefresh = "X";
        constexpr const char *MarketDataRequestReject = "Y";
    }

    // Order Side Values
    namespace OrderSide
    {
        constexpr const char *Buy = "1";
        constexpr const char *Sell = "2";
        constexpr const char *BuyMinus = "3";
        constexpr const char *SellPlus = "4";
        constexpr const char *SellShort = "5";
        constexpr const char *SellShortExempt = "6";
    }

    // Order Type Values
    namespace OrderType
    {
        constexpr const char *Market = "1";
        constexpr const char *Limit = "2";
        constexpr const char *Stop = "3";
        constexpr const char *StopLimit = "4";
        constexpr const char *MarketOnClose = "5";
        constexpr const char *WithOrWithout = "6";
        constexpr const char *LimitOrBetter = "7";
        constexpr const char *LimitWithOrWithout = "8";
        constexpr const char *OnBasis = "9";
    }

    // Time In Force Values
    namespace TimeInForce
    {
        constexpr const char *Day = "0";
        constexpr const char *GoodTillCancel = "1";
        constexpr const char *AtTheOpening = "2";
        constexpr const char *ImmediateOrCancel = "3";
        constexpr const char *FillOrKill = "4";
        constexpr const char *GoodTillCrossing = "5";
        constexpr const char *GoodTillDate = "6";
    }

    // Order Status Values
    namespace OrderStatus
    {
        constexpr const char *New = "0";
        constexpr const char *PartiallyFilled = "1";
        constexpr const char *Filled = "2";
        constexpr const char *DoneForDay = "3";
        constexpr const char *Canceled = "4";
        constexpr const char *Replaced = "5";
        constexpr const char *PendingCancel = "6";
        constexpr const char *Stopped = "7";
        constexpr const char *Rejected = "8";
        constexpr const char *Suspended = "9";
        constexpr const char *PendingNew = "A";
        constexpr const char *Calculated = "B";
        constexpr const char *Expired = "C";
        constexpr const char *AcceptedForBidding = "D";
        constexpr const char *PendingReplace = "E";
    }

    // Execution Type Values
    namespace ExecType
    {
        constexpr const char *New = "0";
        constexpr const char *PartialFill = "1";
        constexpr const char *Fill = "2";
        constexpr const char *DoneForDay = "3";
        constexpr const char *Canceled = "4";
        constexpr const char *Replace = "5";
        constexpr const char *PendingCancel = "6";
        constexpr const char *Stopped = "7";
        constexpr const char *Rejected = "8";
        constexpr const char *Suspended = "9";
        constexpr const char *PendingNew = "A";
        constexpr const char *Calculated = "B";
        constexpr const char *Expired = "C";
        constexpr const char *Restated = "D";
        constexpr const char *PendingReplace = "E";
    }

    // Utility class for field name resolution
    class FieldNames
    {
    public:
        static std::string getFieldName(int fieldTag);
        static int getFieldTag(const std::string &fieldName);

    private:
        static std::unordered_map<int, std::string> tagToName_;
        static std::unordered_map<std::string, int> nameToTag_;
        static void initializeMaps();
    };

    // Helper functions for FIX protocol
    inline std::string formatFixField(int tag, const std::string &value)
    {
        return std::to_string(tag) + "=" + value + FIX_SOH;
    }

    inline std::string formatFixField(int tag, int value)
    {
        return std::to_string(tag) + "=" + std::to_string(value) + FIX_SOH;
    }

    inline std::string formatFixField(int tag, double value, int precision = 2)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
        return std::to_string(tag) + "=" + buffer + FIX_SOH;
    }

} // namespace fix_gateway::protocol