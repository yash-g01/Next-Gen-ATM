#include <iostream>
#include <string>
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

    public:
        Account(int accNum, string accHolder, double bal, int pinCode)
            : accountNumber(accNum), accountHolderName(accHolder), balance(bal), pin(pinCode) {
            }

        // Getters for authentication
        int getAccountNumber() const { return accountNumber; }
        
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
            if (amount > 0) {
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
            } else {
                cout << "\n[ERROR] Insufficient funds or invalid amount." << endl;
            }
        }
};

// Helper function to find an account based on card number
Account* findAccount(Account accounts[], int size, int accNum) {
    for (int i = 0; i < size; i++) {
        if (accounts[i].getAccountNumber() == accNum) {
            return &accounts[i]; // Return pointer to the found account
        }
    }
    return nullptr; // Not found
}

//print QR Code to console
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
    // 1. Setup some dummy accounts (Database simulation) using an Array of Objects
    const int NUM_ACCOUNTS = 4;
    Account bankAccounts[NUM_ACCOUNTS] = {
        Account(1001, "Tanmay Padale", 1800.00, 1234),
        Account(1002, "Swayam Bagul", 1500.50, 5678),
        Account(1003, "Aditya Kamble", 3000.00, 9999),
        Account(1004, "Yash Pratap Gautam", 2000.27, 1111)
    };

    cout << "=== SYSTEM BOOTED ===" << endl;

    // 2. Main ATM Loop (Runs forever until program closed)
    while (true) {
        int mainChoice;

        cout << "\n=================================" << endl;
        cout << "     ATM MACHINE SIMULATION      " << endl;
        cout << "=================================" << endl;

        cout << "1. Enter Account Number" << endl;
        cout << "2. UPI Withdrawal" << endl;
        cout << "Select option: ";
        cin >> mainChoice;

        if (mainChoice == 1) {
            // Authentication Step
            int enteredAccount;
            int enteredPin;
            Account* currentSession = nullptr;
            cout << "Please enter Account Number (or -1 to go back): ";
            cin >> enteredAccount;

            if (enteredAccount <= 0) continue; // Go back to main menu

            currentSession = findAccount(bankAccounts, NUM_ACCOUNTS, enteredAccount);

            if (currentSession != nullptr) {
                cout << "Enter PIN: ";
                cin >> enteredPin;

                if (currentSession->validatePin(enteredPin)) {
                    cout << "\nLogin Successful! Welcome, " << currentSession->getName() << "." << endl;
                    
                    // 3. User Session Loop
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
                            case 1:
                                currentSession->checkBalance();
                                break;
                            case 2: {
                                double amt;
                                cout << "Enter deposit amount: ";
                                cin >> amt;
                                currentSession->deposit(amt);
                                break;
                            }
                            case 3: {
                                double amt;
                                cout << "Enter withdrawal amount: ";
                                cin >> amt;
                                currentSession->withdraw(amt);
                                break;
                            }
                            case 4:
                                cout << "Ejecting card... Goodbye!" << endl;
                                sessionActive = false;
                                break;
                            default:
                                cout << "Invalid option. Please try again." << endl;
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
            cout << "Enter Withdrawal Amount (-1 to go back): ";
            cin >> upiAmt;

            if (upiAmt <= 0) continue; // Go back to main menu

            cout << "Scan the QR code to pay ₹" << upiAmt << endl;

            std::string upiLink = "upi://pay?pa=atm@bank&pn=ATM%20Machine%20Simulation&am="+ std::to_string(upiAmt) +"&cu=INR";

            const QrCode qr0 = QrCode::encodeText(upiLink.c_str(), QrCode::Ecc::LOW);
            printQr(qr0);

            int simInput;
            cout << "Simulation: Enter 1 to confirm payment, 0 to cancel: ";
            cin >> simInput;
            if (simInput == 0) {
                cout << "[CANCELLED] UPI Transaction cancelled. Returning to main menu." << endl;
                continue;
            }
            else {
                cout << "[SUCCESS] Payment received! Dispensing cash..." << endl;
            }
        }

        else {
            cout << "\n[ERROR] Invalid option. Please try again." << endl;
        }
    }
    return 0;
}