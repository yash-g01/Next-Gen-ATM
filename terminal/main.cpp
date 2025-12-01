#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

// --- CROSS-PLATFORM NETWORKING SETUP ---
#ifdef _WIN32
    // Windows-specific headers and libraries
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib") // Link Winsock library automatically on Visual Studio
    
    // Map Windows functions to standard names
    #define CLOSE_SOCKET closesocket
    #define SOCK_TYPE SOCKET
    #define IS_VALIDSOCKET(s) ((s) != INVALID_SOCKET)
#else
    // Linux/Mac-specific headers
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>

    // Map Linux functions to standard names
    #define CLOSE_SOCKET close
    #define SOCK_TYPE int
    #define IS_VALIDSOCKET(s) ((s) >= 0)
#endif

#include "qrcodegen.hpp"

using namespace std;
using std::uint8_t;
using qrcodegen::QrCode;
using qrcodegen::QrSegment;

// --- UTILITY: INITIALIZE WINSOCK (Windows Only) ---
bool initNetworking() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "[ERROR] WSAStartup failed." << endl;
        return false;
    }
#endif
    return true;
}

// --- UTILITY: CLEANUP WINSOCK (Windows Only) ---
void cleanupNetworking() {
#ifdef _WIN32
    WSACleanup();
#endif
}

class Account {
    private:
        int accountNumber;
        string accountHolderName;
        double balance;
        int pin;
        long long cardNumber;

    public:
        Account(int accNum, string accHolder, double bal, int pinCode, long long cardNum)
            : accountNumber(accNum), accountHolderName(accHolder), balance(bal), pin(pinCode), cardNumber(cardNum) {
            }

        int getAccountNumber() const { return accountNumber; }
        long long getCardNumber() const { return cardNumber; }
        bool validatePin(int enteredPin) const { return pin == enteredPin; }
        string getName() const { return accountHolderName; }

        void checkBalance() const {
             cout << "\n--- Account Status ---" << endl;
             cout << "Holder: " << accountHolderName << endl;
             cout << "Current Balance: " << balance << endl; // Removed currency symbol for console compatibility
             cout << "----------------------" << endl;
        }

        void deposit(int amount) {
            if (amount > 0 && amount % 100 == 0) {
                balance += amount;
                cout << "\n[SUCCESS] Deposited " << amount << endl;
                cout << "New Balance: " << balance << endl;
            } else {
                cout << "\n[ERROR] Deposit amount must be positive." << endl;
            }
        }

        void withdraw(int amount) {
            if (amount > 0 && amount <= balance && amount % 100 == 0) {
                balance -= amount;
                cout << "\n[SUCCESS] Please take your cash: " << amount << endl;
                cout << "New Balance: " << balance << endl;
                cout << "Transaction Complete." << endl;
            } else {
                cout << "\n[ERROR] Insufficient funds or invalid amount." << endl;
            }
        }
};

Account* findAccount(Account accounts[], int size, long long accNum) {
    for (int i = 0; i < size; i++) {
        if (accounts[i].getAccountNumber() == accNum) {
            return &accounts[i];
        }
    }
    return nullptr;
}

Account* findCardNum(Account accounts[], int size, long long cardNum) {
    for (int i = 0; i < size; i++) {
        if (accounts[i].getCardNumber() == cardNum) {
            return &accounts[i];
        }
    }
    return nullptr;
}

string parseJsonValue(string body, string key) {
    string searchKey = "\"" + key + "\"";
    size_t keyPos = body.find(searchKey);
    
    if (keyPos == string::npos) return "";

    size_t colonPos = body.find(":", keyPos);
    if (colonPos == string::npos) return "";

    size_t valueStart = colonPos + 1;
    size_t valueEnd = body.find_first_of(",}", valueStart);
    if (valueEnd == string::npos) valueEnd = body.length();

    string value = body.substr(valueStart, valueEnd - valueStart);
    value.erase(remove(value.begin(), value.end(), '\"'), value.end());
    value.erase(remove(value.begin(), value.end(), ' '), value.end());
    value.erase(remove(value.begin(), value.end(), '\n'), value.end());
    value.erase(remove(value.begin(), value.end(), '\r'), value.end());

    return value;
}

// --- UPDATED SERVER FUNCTION ---
string startNFCServer() {
    SOCK_TYPE server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[2048] = {0}; 

    cout << "\n[NFC] Starting Receiver on Port 12345..." << endl;
    
    // Use the abstraction for creating socket
    if (!IS_VALIDSOCKET(server_fd = socket(AF_INET, SOCK_STREAM, 0))) {
        perror("[NFC] Socket failed");
        return "";
    }

    // Setsockopt is slightly different on Windows (char* vs int*)
#ifdef _WIN32
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt))) {
#else
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
#endif
        perror("[NFC] Setsockopt failed");
        return "";
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(12345);

    if (::bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("[NFC] Bind failed");
        return "";
    }
    
    if (listen(server_fd, 3) < 0) {
        perror("[NFC] Listen failed");
        return "";
    }
    
    cout << "[NFC] Waiting for Card Tap (Connection from Phone)..." << endl;
    
    if (!IS_VALIDSOCKET(new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))) {
        perror("[NFC] Accept failed");
        return "";
    }

    cout << "[NFC] Connection Established! Reading data..." << endl;

    // Use recv() instead of read() for compatibility
    recv(new_socket, buffer, 2048, 0);
    string rawRequest(buffer);
    
    string httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nOK";
    send(new_socket, httpResponse.c_str(), httpResponse.length(), 0);

    string jsonBody = "";
    size_t bodyPos = rawRequest.find("\r\n\r\n");
    if (bodyPos != string::npos) {
        jsonBody = rawRequest.substr(bodyPos + 4);
    } else {
        jsonBody = rawRequest; 
    }

    cout << "[DEBUG] Raw Body: " << jsonBody << endl;

    string cardNum = parseJsonValue(jsonBody, "cardNum");

    cout << "[NFC] Parsed Card Number: " << cardNum << endl;
    
    // Cross-platform cleanup
    CLOSE_SOCKET(new_socket);
    CLOSE_SOCKET(server_fd);
    
    return cardNum;
}

static void printQr(const QrCode &qr) {
    int border = 1;
    for (int y = -border; y < qr.getSize() + border; y++) {
        for (int x = -border; x < qr.getSize() + border; x++) {
            // Note: Some Windows consoles might struggle with these unicode blocks
            // Use simple # and space if that happens
            std::cout << (qr.getModule(x, y) ? "\u2588\u2588" : "  ");
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

int main() {
    // 1. Initialize Networking (Required for Windows)
    if (!initNetworking()) return 1;

    const int NUM_ACCOUNTS = 3;
    Account bankAccounts[NUM_ACCOUNTS] = {
        Account(1001, "Tanmay Padale", 1800.00, 1234, 7864), 
        Account(1002, "Swayam Bagul", 1500.50, 5678, 8420),
        Account(1004, "Yash Pratap Gautam", 2000.27, 1111, 5887)
    };

    while (true) {
        int mainChoice;

        cout << "\n=================================" << endl;
        cout << "      ATM MACHINE SIMULATION      " << endl;
        cout << "=================================" << endl;

        cout << "1. Enter Account Number" << endl;
        cout << "2. UPI Withdrawal" << endl;
        cout << "3. Tap & Withdraw (NFC)" << endl; 
        cout << "Select option: ";
        cin >> mainChoice;

        if (mainChoice == 1) {
            int enteredAccount;
            int enteredPin;
            Account* currentSession = nullptr;
            cout << "Please enter Account Number (or <=0 to back): ";
            cin >> enteredAccount;

            if (enteredAccount <= 0) continue;

            currentSession = findAccount(bankAccounts, NUM_ACCOUNTS, enteredAccount);

            if (currentSession != nullptr) {
                cout << "Enter PIN: ";
                cin >> enteredPin;

                if (currentSession->validatePin(enteredPin)) {
                    cout << "\nLogin Successful! Welcome, " << currentSession->getName() << "." << endl;
                    
                    bool sessionActive = true;
                    while (sessionActive) {
                        int choice;
                        cout << "\n--- Main Menu ---" << endl;
                        cout << "1. Check Balance" << endl;
                        cout << "2. Deposit Cash" << endl;
                        cout << "3. Withdraw Cash" << endl;
                        cout << "4. Eject Card" << endl;
                        cout << "Select option: ";
                        cin >> choice;

                        switch (choice) {
                            case 1: currentSession->checkBalance(); break;
                            case 2: {
                                double amt; cout << "Enter deposit amount: "; cin >> amt;
                                currentSession->deposit(amt); break;
                            }
                            case 3: {
                                double amt; cout << "Enter withdrawal amount: "; cin >> amt;
                                currentSession->withdraw(amt); break;
                            }
                            case 4: cout << "Ejecting card... Goodbye!" << endl; sessionActive = false; break;
                            default: cout << "Invalid option." << endl;
                        }
                    }
                } else {
                    cout << "\n[ERROR] Invalid PIN." << endl;
                }
            } else {
                cout << "\n[ERROR] Account not recognized." << endl;
            }
        }

        else if (mainChoice == 2) {
            int upiAmt;
            cout << "Enter Withdrawal Amount (Enter <= 0 to go back): ";
            cin >> upiAmt;

            if (upiAmt <= 0) continue;

            cout << "Scan the QR code to pay " << upiAmt << endl;
            std::string upiLink = "upi://pay?pa=atm@bank&pn=ATM&am="+ std::to_string(upiAmt) +"&cu=INR";
            const QrCode qr0 = QrCode::encodeText(upiLink.c_str(), QrCode::Ecc::LOW);
            printQr(qr0);

            int simInput;
            cout << "Simulation: Enter 1 to confirm, 0 to cancel: ";
            cin >> simInput;
            if (simInput == 1) cout << "[SUCCESS] Payment received! Dispensing cash..." << endl;
            else cout << "[CANCELLED]" << endl;
        }

        else if (mainChoice == 3) {
            string nfcData = startNFCServer();
            
            if (nfcData == "") {
                cout << "[ERROR] NFC Read Failed or Cancelled." << endl;
                continue;
            }

            try {
                long long cardNum = stoll(nfcData);
                Account* currentSession = findCardNum(bankAccounts, NUM_ACCOUNTS, cardNum);

                if (currentSession != nullptr) {
                    cout << "\n[NFC] Card Detected: " << currentSession->getName() << endl;
                    
                    int enteredPin;
                    cout << "Enter PIN to verify: ";
                    cin >> enteredPin;

                    if (currentSession->validatePin(enteredPin)) {
                        int withdrawAmount;
                        cout << "PIN Verified." << endl;
                        cout << "Enter Amount to Withdraw: ";
                        cin >> withdrawAmount;
                        
                        currentSession->withdraw(withdrawAmount);
                    } else {
                        cout << "\n[ERROR] Invalid PIN. Transaction Cancelled." << endl;
                    }
                } else {
                    cout << "\n[ERROR] Card Number" << cardNum << ") not found in database." << endl;
                }
            } catch (...) {
                cout << "[ERROR] Invalid data received from NFC tag." << endl;
            }
        }

        else {
            cout << "\n[ERROR] Invalid option." << endl;
        }
    }
    
    // Cleanup Windows Networking before exiting
    cleanupNetworking();
    return 0;
}