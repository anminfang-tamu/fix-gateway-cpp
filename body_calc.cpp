#include <iostream>
#include <string>

int main() {
    std::string body = "35=D\x01";                    // MsgType
    body += "49=SENDER\x01";                         // SenderCompID  
    body += "56=TARGET\x01";                         // TargetCompID
    body += "52=20231201-12:00:00.000\x01";          // SendingTime
    body += "11=ORDER123\x01";                       // ClOrdID
    body += "55=AAPL\x01";                           // Symbol
    body += "54=1\x01";                              // Side
    body += "38=100\x01";                            // OrderQty
    body += "44=150.25\x01";                         // Price
    body += "40=2\x01";                              // OrdType
    body += "59=0\x01";                              // TimeInForce
    
    std::cout << "Body: ";
    for (char c : body) {
        if (c == '\x01') std::cout << "<SOH>";
        else std::cout << c;
    }
    std::cout << std::endl;
    std::cout << "Body length: " << body.length() << " bytes" << std::endl;
    
    return 0;
}
