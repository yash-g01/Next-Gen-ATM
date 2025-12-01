#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>

#include "qrcodegen.hpp"

using namespace std;
using std::uint8_t;
using qrcodegen::QrCode;
using qrcodegen::QrSegment;

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

        // Getters for authentication
        int getAccountNumber() const { return accountNumber; }

        long long getCardNumber() const { return cardNumber; }
        
        bool validatePin(int enteredPin) const { return pin == enteredPin; }
        
        string getName() const { return accountHolderName; }

        // Transaction Methods
        void checkBalance() const {
             cout << "\n--- Account Status ---" << endl;
             cout << "Holder: " << accountHolderName << endl;
             cout << "Current Balance: ₹" << balance << endl;
             cout << "----------------------" << endl;
        }

        void deposit(int amount) {
            if (amount > 0 && amount % 100 == 0) {
                balance += amount;
                cout << "\n[SUCCESS] Deposited ₹" << amount << endl;
                cout << "New Balance: ₹" << balance << endl;
            } else {
                cout << "\n[ERROR] Deposit amount must be positive." << endl;
            }
        }

        void withdraw(int amount) {
            if (amount > 0 && amount <= balance && amount % 100 == 0) {
                balance -= amount;
                cout << "\n[SUCCESS] Please take your cash: ₹" << amount << endl;
                cout << "New Balance: ₹" << balance << endl;
                cout << "Transaction Complete." << endl;
            } else {
                cout << "\n[ERROR] Insufficient funds or invalid amount." << endl;
            }
        }
};

// Helper function to find an account based on account number
Account* findAccount(Account accounts[], int size, long long accNum) {
    for (int i = 0; i < size; i++) {
        if (accounts[i].getAccountNumber() == accNum) {
            return &accounts[i]; // Return pointer to the found account
        }
    }
    return nullptr; // Not found
}

// Helper function to find an card based on card number
Account* findCardNum(Account accounts[], int size, long long cardNum) {
    for (int i = 0; i < size; i++) {
        if (accounts[i].getCardNumber() == cardNum) {
            return &accounts[i]; // Return pointer to the found account
        }
    }
    return nullptr; // Not found
}

// Helper: Parse JSON value from raw HTTP body
// Looks for "key" and returns the value after the colon
string parseJsonValue(string body, string key) {
    string searchKey = "\"" + key + "\"";
    size_t keyPos = body.find(searchKey);
    
    if (keyPos == string::npos) return "";

    // Find the colon after the key
    size_t colonPos = body.find(":", keyPos);
    if (colonPos == string::npos) return "";

    // Find the start of the value
    size_t valueStart = colonPos + 1;
    
    // Find end of the value (comma or closing brace)
    size_t valueEnd = body.find_first_of(",}", valueStart);
    if (valueEnd == string::npos) valueEnd = body.length();

    string value = body.substr(valueStart, valueEnd - valueStart);

    // Clean up (remove quotes, spaces, newlines)
    value.erase(remove(value.begin(), value.end(), '\"'), value.end());
    value.erase(remove(value.begin(), value.end(), ' '), value.end());
    value.erase(remove(value.begin(), value.end(), '\n'), value.end());
    value.erase(remove(value.begin(), value.end(), '\r'), value.end());

    return value;
}

// Helper: Start TCP Server and wait for 1 connection
string startNFCServer() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[2048] = {0}; // Increased buffer size for HTTP Headers

    cout << "\n[NFC] Starting Receiver on Port 12345..." << endl;
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("[NFC] Socket failed");
        return "";
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
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
    
    cout << "[NFC] Waiting for Card Tap (Connection from iPhone)..." << endl;
    
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("[NFC] Accept failed");
        return "";
    }

    cout << "[NFC] Connection Established! Reading data..." << endl;

    // 6. Read Data
    read(new_socket, buffer, 2048);
    string rawRequest(buffer);
    
    // 7. Send HTTP Response back to iPhone (Otherwise Shortcut fails)
    string httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nOK";
    send(new_socket, httpResponse.c_str(), httpResponse.length(), 0);

    // 8. Extract Body from HTTP Request
    // The body is separated from headers by a double newline (\r\n\r\n)
    string jsonBody = "";
    size_t bodyPos = rawRequest.find("\r\n\r\n");
    if (bodyPos != string::npos) {
        jsonBody = rawRequest.substr(bodyPos + 4);
    } else {
        jsonBody = rawRequest; // Fallback if no headers found
    }

    cout << "[DEBUG] Raw Body: " << jsonBody << endl;

    // 9. Parse "cardNum" from JSON
    string cardNum = parseJsonValue(jsonBody, "cardNum");

    cout << "[NFC] Parsed Card Number: " << cardNum << endl;
    
    // Cleanup
    close(new_socket);
    close(server_fd);
    
    return cardNum;
}

// Print QR Code to console
static void printQr(const QrCode &qr) {
	int border = 1;
	for (int y = -border; y < qr.getSize() + border; y++) {
		for (int x = -border; x < qr.getSize() + border; x++) {
			std::cout << (qr.getModule(x, y) ? "\u2588\u2588" : "  ");
		}
		std::cout << std::endl;
	}
	std::cout << std::endl;
}

int main() {
    // 1. Setup dummy accounts with LONG LONG for big card numbers
    const int NUM_ACCOUNTS = 4;
    Account bankAccounts[NUM_ACCOUNTS] = {
        // UPDATED: Using the number from your screenshot for Tanmay
        Account(1001, "Tanmay Padale", 1800.00, 1234, 1234567890123456), 
        Account(1002, "Swayam Bagul", 1500.50, 5678, 2345678901234567),
        Account(1003, "Aditya Kamble", 3000.00, 9999, 3456789012345678),
        Account(1004, "Yash Pratap Gautam", 2000.27, 1111, 5085461132745887)
    };

    // 2. Main ATM Loop
    while (true) {
        int mainChoice;

        cout << "\n=================================" << endl;
        cout << "     ATM MACHINE SIMULATION      " << endl;
        cout << "=================================" << endl;

        cout << "1. Enter Account Number" << endl;
        cout << "2. UPI Withdrawal" << endl;
        cout << "3. Tap & Withdraw (NFC)" << endl; 
        cout << "Select option: ";
        cin >> mainChoice;

        // --- OPTION 1: Manual Login ---
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

        // --- OPTION 2: UPI ---
        else if (mainChoice == 2) {
            int upiAmt;
            cout << "Enter Withdrawal Amount (Enter <= 0 to go back): ";
            cin >> upiAmt;

            if (upiAmt <= 0) continue;

            cout << "Scan the QR code to pay ₹" << upiAmt << endl;
            std::string upiLink = "upi://pay?pa=atm@bank&pn=ATM&am="+ std::to_string(upiAmt) +"&cu=INR";
            const QrCode qr0 = QrCode::encodeText(upiLink.c_str(), QrCode::Ecc::LOW);
            printQr(qr0);

            int simInput;
            cout << "Simulation: Enter 1 to confirm, 0 to cancel: ";
            cin >> simInput;
            if (simInput == 1) cout << "[SUCCESS] Payment received! Dispensing cash..." << endl;
            else cout << "[CANCELLED]" << endl;
        }

        // --- OPTION 3: NFC TAP & WITHDRAW ---
        else if (mainChoice == 3) {
            string nfcData = startNFCServer();
            
            if (nfcData == "") {
                cout << "[ERROR] NFC Read Failed or Cancelled." << endl;
                continue;
            }

            try {
                // Convert string to long long
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
    return 0;
}