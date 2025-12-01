#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QStackedLayout>
#include <QInputDialog>
#include <QString>
#include <QTimer>
#include <QPainter>
#include <QDoubleValidator>
#include <QThread>
#include <QDebug>
#include <QByteArray>
#include <QFrame>
#include <QGraphicsDropShadowEffect>

// --- Qt Network Includes (Cross-Platform) ---
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>

#include <vector>
#include <string>
#include <sstream>
#include <cstring>
#include <climits>
#include <algorithm>
#include <stdexcept>

#include "nfcworker.h"
#include "qrcodegen.hpp"

using std::uint8_t;
using qrcodegen::QrCode;
using qrcodegen::QrSegment;
using namespace std;

// ==========================================
// 1. Logic Layer (The Account Class)
// ==========================================
class Account {
private:
    int accountNumber;
    QString accountHolderName;
    double balance;
    int pin;
    long long cardNumber;

public:
    Account(int accNum = 0, QString accHolder = "", double bal = 0.0, int pinCode = 0, long long cardNum = 0)
        : accountNumber(accNum), accountHolderName(accHolder), balance(bal), pin(pinCode), cardNumber(cardNum) {
    }

    int getAccountNumber() const { return accountNumber; }
    long long getCardNumber() const { return cardNumber; }
    bool validatePin(int enteredPin) const { return pin == enteredPin; }
    QString getName() const { return accountHolderName; }
    double getBalance() const { return balance; }

    bool deposit(int amount) {
        if (amount > 0 && amount % 100 == 0) {
            balance += amount;
            return true;
        }
        return false;
    }

    bool withdraw(int amount) {
        if (amount > 0 && amount <= balance && amount % 100 == 0) {
            balance -= amount;
            return true;
        }
        return false;
    }
};

// ==========================================
// 2. NFC Worker Thread (Cross-Platform Qt Network)
// ==========================================
void NfcWorker::run() {
    QTcpServer server;

    if (!server.listen(QHostAddress::Any, 12345)) {
        emit errorOccurred("Unable to start NFC Server: " + server.errorString());
        return;
    }

    qDebug() << "[NFC] Waiting for connection on port 12345...";

    const int totalTimeoutMs = 30000;
    const int stepMs = 100;
    int elapsed = 0;
    QTcpSocket *socket = nullptr;

    while (elapsed < totalTimeoutMs && !isInterruptionRequested()) {
        if (server.waitForNewConnection(stepMs)) {
            socket = server.nextPendingConnection();
            break;
        }
        elapsed += stepMs;
    }

    if (isInterruptionRequested()) {
        emit errorOccurred("NFC cancelled by user.");
        server.close();
        return;
    }

    if (!socket) {
        emit errorOccurred("Timeout: No card tapped.");
        server.close();
        return;
    }

    if (socket->waitForReadyRead(3000)) {
        QByteArray requestData = socket->readAll();
        string rawRequest = requestData.toStdString();

        QString httpResponse = "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: 2\r\n"
                               "\r\nOK";
        socket->write(httpResponse.toUtf8());
        socket->waitForBytesWritten(1000);
        socket->disconnectFromHost();

        string jsonBody = "";
        size_t bodyPos = rawRequest.find("\r\n\r\n");
        if (bodyPos != string::npos) {
            jsonBody = rawRequest.substr(bodyPos + 4);
        } else {
            jsonBody = rawRequest;
        }

        string cardNumStr = parseJsonValue(jsonBody, "cardNum");

        if (!cardNumStr.empty()) {
            try {
                long long cardNum = stoll(cardNumStr);
                emit cardDetected(cardNum);
            } catch (...) {
                emit errorOccurred("Invalid number format");
            }
        } else {
            emit errorOccurred("No cardNum found in request");
        }
    }

    socket->close();
    delete socket;
    server.close();
}

std::string NfcWorker::parseJsonValue(std::string body, std::string key) {
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

// ==========================================
// 3. GUI Layer (The Main Window)
// ==========================================
class ATMWindow : public QWidget {
private:
    // Data
    Account accounts[3];
    Account* currentSession = nullptr;
    Account* pendingAccount = nullptr;

    // NFC Thread
    NfcWorker* nfcThread;

    // GUI Components
    QStackedLayout* stackedLayout;

    // Pages
    QWidget* loginPage;
    QWidget* mainPage;
    QWidget* upiAmountPage;
    QWidget* qrDisplayPage;

    // Login Widgets
    QLabel* titleLabel;
    QLineEdit* accInput;
    QPushButton* verifyAccBtn;
    QLabel* userLabel;
    QLineEdit* pinInput;
    QPushButton* loginBtn;
    QPushButton* cancelBtn;
    QPushButton* upiLoginBtn;
    QPushButton* nfcLoginBtn;
    QLabel* nfcStatusLabel;
    QLabel* nfcCardImage;

    // Main Menu Widgets
    QLabel* welcomeLabel;
    QLabel* balanceLabel;

    // UPI Widgets
    QLineEdit* upiAmtInput;
    QLabel* qrImageLabel;
    QLabel* timerLabel;
    QTimer* upiTimer;
    int timeLeft;

public:
    ATMWindow() {
        this->setObjectName("ATMWindow");
        // Initialize Data
        accounts[0] = Account(1001, "Tanmay Ravindra Padale", 1800.00, 1234, 8825);
        accounts[1] = Account(1002, "Swayam Bagul", 1500.50, 5678, 7787);
        accounts[2] = Account(1003, "Yash Pratap Gautam", 2000.00, 1111, 5887);

        upiTimer = new QTimer(this);
        connect(upiTimer, &QTimer::timeout, this, &ATMWindow::updateTimer);

        nfcThread = new NfcWorker();
        connect(nfcThread, &NfcWorker::cardDetected, this, &ATMWindow::handleNfcSuccess);
        connect(nfcThread, &NfcWorker::errorOccurred, this, [this](QString msg){
            qDebug() << "NFC Error:" << msg;
            if(nfcStatusLabel->isVisible()) {
                resetLoginUI();
            }
        });

        setupUI();
        applyModernStyle();
    }

    ~ATMWindow() {
        if(nfcThread->isRunning()) {
            nfcThread->requestInterruption();
            nfcThread->quit();
            nfcThread->wait();
        }
    }

private:
    void applyModernStyle() {
        QString styleSheet = R"(
            QWidget {
                font-family: 'Segoe UI', 'Roboto', sans-serif;
            }

            /* 1. OUTER WINDOW (Now has the matching gradient) */
            #ATMWindow {
                /* The Midnight Blue Gradient */
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0f2027, stop:0.5 #203a43, stop:1 #2c5364);
            }

            /* 2. INNER UI CONTAINER */
            QWidget#MainContainer {
            }

            /* --- REMAINDER OF YOUR STYLES --- */
            QLineEdit {
                background-color: rgba(255, 255, 255, 0.1);
                border: 1px solid rgba(255, 255, 255, 0.3);
                border-radius: 25px;
                padding: 12px 20px;
                color: #ffffff;
                font-size: 16px;
                font-weight: 500;
            }
            QLineEdit:focus {
                border: 2px solid #00d2ff;
                background-color: rgba(255, 255, 255, 0.15);
            }

            QPushButton {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #1FA2FF, stop:0.5 #12D8FA, stop:1 #A6FFCB);
                color: #000000;
                border: none;
                border-radius: 25px;
                padding: 14px;
                font-size: 16px;
                font-weight: bold;
            }
            QPushButton:hover {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #12D8FA, stop:1 #1FA2FF);
                margin-top: -2px;
            }
            QPushButton:pressed {
                background: #0b8793;
                margin-top: 2px;
            }

            QPushButton#logoutBtn, QPushButton#cancelBtn {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #cb2d3e, stop:1 #ef473a);
                color: white;
            }
            QPushButton#logoutBtn:hover, QPushButton#cancelBtn:hover { background: #ff5e57; }

            QPushButton#upiBtn {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #8E2DE2, stop:1 #4A00E0);
                color: white;
            }

            QPushButton#nfcBtn {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #F2994A, stop:1 #F2C94C);
                color: #333;
            }

            QLabel { color: #ecf0f1; }

            QLabel#header {
                font-size: 28px;
                font-weight: 800;
                color: #ffffff;
                margin-bottom: 25px;
            }

            QLabel#subHeader {
                font-size: 14px;
                color: #bdc3c7;
                letter-spacing: 1px;
                text-transform: uppercase;
            }

            QLabel#balance {
                font-size: 42px;
                font-weight: bold;
                color: #2ecc71;
                margin: 20px 0;
            }

            QLabel#timer {
                font-size: 48px;
                font-weight: bold;
                color: #f1c40f;
            }

            QLabel#nfcStatus {
                font-size: 18px;
                color: #f39c12;
                font-style: italic;
                font-weight: bold;
                margin: 15px 0;
            }

            QFrame#ContentCard {
                background-color: rgba(0, 0, 0, 0.4);
                border-radius: 20px;
                border: 1px solid rgba(255,255,255,0.1);
            }
        )";
        this->setStyleSheet(styleSheet);
    }

    // Helper to create a styled container for pages
    QFrame* createPageContainer(QVBoxLayout*& outLayout) {
        QFrame* frame = new QFrame;
        frame->setObjectName("ContentCard");
        outLayout = new QVBoxLayout(frame);
        outLayout->setContentsMargins(30, 40, 30, 40);
        outLayout->setSpacing(15);

        // Add shadow effect to the card
        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(frame);
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(0, 0, 0, 80));
        shadow->setOffset(0, 8);
        frame->setGraphicsEffect(shadow);

        return frame;
    }

    void setupUI() {
        // 1. The Main Window is now RESIZABLE
        this->setWindowTitle("Bank ATM");
        this->setMinimumSize(550, 800); // Prevent user from crushing the UI

        // 2. Create the "Inner UI" Container (Fixed Size)
        // This acts as the "Phone Screen" or "ATM Screen"
        QWidget* fixedContentWidget = new QWidget;
        fixedContentWidget->setObjectName("MainContainer"); // Applies the gradient style
        fixedContentWidget->setFixedSize(480, 750); // <--- FIXED SIZE FOR UI ONLY

        // 3. Setup Layout inside the Fixed Container
        QVBoxLayout* fixedLayout = new QVBoxLayout(fixedContentWidget);
        fixedLayout->setContentsMargins(0, 0, 0, 0);

        stackedLayout = new QStackedLayout;
        fixedLayout->addLayout(stackedLayout);

        // 4. Create Pages (Standard logic)
        createLoginPage();
        createMainPage();
        createUPIAmountPage();
        createQRDisplayPage();

        stackedLayout->addWidget(loginPage);
        stackedLayout->addWidget(mainPage);
        stackedLayout->addWidget(upiAmountPage);
        stackedLayout->addWidget(qrDisplayPage);

        // 5. Main Window Layout (Centers the Fixed Container)
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(fixedContentWidget);

        // This ensures the 480x750 widget stays in the center while window resizes
        mainLayout->setAlignment(fixedContentWidget, Qt::AlignCenter);
    }

    // --- Page 0: Login ---
    void createLoginPage() {
        loginPage = new QWidget;
        QVBoxLayout* pageLayout = new QVBoxLayout(loginPage);
        pageLayout->setAlignment(Qt::AlignCenter);

        QVBoxLayout* cardLayout;
        QFrame* card = createPageContainer(cardLayout);

        // Logo/Header area
        titleLabel = new QLabel("BANK ATM");
        titleLabel->setObjectName("header");

        QLabel* subLabel = new QLabel("SECURE TRANSACTION SYSTEM");
        subLabel->setObjectName("subHeader");
        subLabel->setAlignment(Qt::AlignCenter);

        // --- Image ---
        nfcCardImage = new QLabel();
        nfcCardImage->setAlignment(Qt::AlignCenter);
        QPixmap cardPix(":/images/nfc_card.png");
        if (!cardPix.isNull()) {
            nfcCardImage->setPixmap(cardPix.scaled(280, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            nfcCardImage->setText("💳");
            nfcCardImage->setStyleSheet("font-size: 80px; color: rgba(255,255,255,0.2);");
        }
        nfcCardImage->setVisible(false);

        // Inputs
        accInput = new QLineEdit;
        accInput->setPlaceholderText("👤  Account Number");
        accInput->setAlignment(Qt::AlignCenter);

        verifyAccBtn = new QPushButton("Verify Account");

        userLabel = new QLabel("");
        userLabel->setObjectName("header"); // Reuse header style for big name
        userLabel->setAlignment(Qt::AlignCenter);
        userLabel->setVisible(false);

        pinInput = new QLineEdit;
        pinInput->setPlaceholderText("🔒  Enter 4-Digit PIN");
        pinInput->setEchoMode(QLineEdit::Password);
        pinInput->setAlignment(Qt::AlignCenter);
        pinInput->setVisible(false);

        loginBtn = new QPushButton("Access Account ➜");
        loginBtn->setVisible(false);

        cancelBtn = new QPushButton("✕ Cancel");
        cancelBtn->setObjectName("cancelBtn");
        cancelBtn->setVisible(false);

        nfcStatusLabel = new QLabel("📡 Tap Card on Reader...");
        nfcStatusLabel->setObjectName("nfcStatus");
        nfcStatusLabel->setAlignment(Qt::AlignCenter);
        nfcStatusLabel->setVisible(false);

        upiLoginBtn = new QPushButton("📱 UPI Withdrawal");
        upiLoginBtn->setObjectName("upiBtn");

        nfcLoginBtn = new QPushButton("💳 Tap Card (NFC)");
        nfcLoginBtn->setObjectName("nfcBtn");

        // Logic
        connect(nfcLoginBtn, &QPushButton::clicked, [this]() {
            accInput->setVisible(false);
            verifyAccBtn->setVisible(false);
            upiLoginBtn->setVisible(false);
            nfcLoginBtn->setVisible(false);
            nfcStatusLabel->setVisible(true);
            cancelBtn->setVisible(true);
            nfcCardImage->setVisible(true);
            if (!nfcThread->isRunning()) nfcThread->start();
        });

        connect(verifyAccBtn, &QPushButton::clicked, [this]() {
            int accNum = accInput->text().toInt();
            pendingAccount = nullptr;
            for (int i = 0; i < 3; i++) {
                if (accounts[i].getAccountNumber() == accNum) {
                    pendingAccount = &accounts[i];
                    break;
                }
            }
            if (pendingAccount) switchToPinMode();
            else QMessageBox::critical(this, "Error", "Account Number not found.");
        });

        connect(loginBtn, &QPushButton::clicked, [this]() {
            if (!pendingAccount) return;
            if (pendingAccount->validatePin(pinInput->text().toInt())) {
                currentSession = pendingAccount;
                refreshDashboard();
                stackedLayout->setCurrentIndex(1);
                resetLoginUI();
            } else {
                QMessageBox::critical(this, "Access Denied", "Invalid PIN code.");
                pinInput->clear();
            }
        });

        connect(cancelBtn, &QPushButton::clicked, [this]() {
            if (nfcThread->isRunning()) {
                nfcThread->requestInterruption();
                nfcThread->wait();
            }
            resetLoginUI();
        });

        connect(upiLoginBtn, &QPushButton::clicked, [this]() {
            upiAmtInput->clear();
            stackedLayout->setCurrentIndex(2);
        });

        // Add to Card
        cardLayout->addWidget(titleLabel);
        cardLayout->addWidget(subLabel);
        cardLayout->addSpacing(20);
        cardLayout->addWidget(nfcCardImage);
        cardLayout->addWidget(accInput);
        cardLayout->addWidget(verifyAccBtn);
        cardLayout->addWidget(userLabel);
        cardLayout->addWidget(pinInput);
        cardLayout->addWidget(loginBtn);
        cardLayout->addWidget(nfcStatusLabel);
        cardLayout->addWidget(cancelBtn);
        cardLayout->addSpacing(15);
        cardLayout->addWidget(upiLoginBtn);
        cardLayout->addWidget(nfcLoginBtn);

        pageLayout->addWidget(card);
    }

    void handleNfcSuccess(long long cardNum) {
        pendingAccount = nullptr;
        for (int i = 0; i < 3; i++) {
            if (accounts[i].getCardNumber() == cardNum) {
                pendingAccount = &accounts[i];
                break;
            }
        }
        if (pendingAccount) {
            nfcStatusLabel->setVisible(false);
            switchToPinMode();
        } else {
            QMessageBox::warning(this, "NFC Error", "Card detected but not recognized in database.");
            resetLoginUI();
        }
    }

    void switchToPinMode() {
        accInput->setVisible(false);
        verifyAccBtn->setVisible(false);
        upiLoginBtn->setVisible(false);
        nfcLoginBtn->setVisible(false);
        nfcStatusLabel->setVisible(false);
        nfcCardImage->setVisible(false);

        titleLabel->setText("AUTHENTICATION");
        userLabel->setText("Hi, " + pendingAccount->getName());
        userLabel->setVisible(true);
        pinInput->setVisible(true);
        loginBtn->setVisible(true);
        cancelBtn->setVisible(true);
        pinInput->setFocus();
    }

    // --- Page 1: Main Menu ---
    void createMainPage() {
        mainPage = new QWidget;
        QVBoxLayout* pageLayout = new QVBoxLayout(mainPage);
        pageLayout->setAlignment(Qt::AlignCenter);

        QVBoxLayout* cardLayout;
        QFrame* card = createPageContainer(cardLayout);

        welcomeLabel = new QLabel("Welcome");
        welcomeLabel->setObjectName("header");

        QLabel* balanceTitle = new QLabel("Available Balance");
        balanceTitle->setObjectName("subHeader");
        balanceTitle->setAlignment(Qt::AlignCenter);

        balanceLabel = new QLabel("₹0.00");
        balanceLabel->setObjectName("balance");
        balanceLabel->setAlignment(Qt::AlignCenter);

        QPushButton* depositBtn = new QPushButton("💰  Deposit Funds");
        QPushButton* withdrawBtn = new QPushButton("💸  Withdraw Cash");
        QPushButton* logoutBtn = new QPushButton("⏏  Eject Card");
        logoutBtn->setObjectName("logoutBtn");

        connect(depositBtn, &QPushButton::clicked, [this]() {
            bool ok;
            double amount = QInputDialog::getDouble(this, "Deposit", "Amount:", 0, 0, 10000, 2, &ok);
            if (ok && currentSession && currentSession->deposit(amount)) {
                QMessageBox::information(this, "Success", "Funds Deposited.");
                refreshDashboard();
            }
        });

        connect(withdrawBtn, &QPushButton::clicked, [this]() {
            bool ok;
            double amount = QInputDialog::getDouble(this, "Withdraw", "Amount:", 0, 0, 10000, 2, &ok);
            if (ok && currentSession) {
                if (currentSession->withdraw(amount)) {
                    QMessageBox::information(this, "Success", "Please take your cash.");
                    refreshDashboard();
                } else {
                    QMessageBox::warning(this, "Error", "Insufficient funds.");
                }
            }
        });

        connect(logoutBtn, &QPushButton::clicked, [this]() {
            currentSession = nullptr;
            resetLoginUI();
            stackedLayout->setCurrentIndex(0);
        });

        cardLayout->addWidget(welcomeLabel);
        cardLayout->addSpacing(10);
        cardLayout->addWidget(balanceTitle);
        cardLayout->addWidget(balanceLabel);
        cardLayout->addSpacing(20);
        cardLayout->addWidget(depositBtn);
        cardLayout->addWidget(withdrawBtn);
        cardLayout->addSpacing(20);
        cardLayout->addWidget(logoutBtn);

        pageLayout->addWidget(card);
    }

    // --- Page 2: UPI Amount Input ---
    void createUPIAmountPage() {
        upiAmountPage = new QWidget;
        QVBoxLayout* pageLayout = new QVBoxLayout(upiAmountPage);
        pageLayout->setAlignment(Qt::AlignCenter);

        QVBoxLayout* cardLayout;
        QFrame* card = createPageContainer(cardLayout);

        QLabel* header = new QLabel("UPI WITHDRAWAL");
        header->setObjectName("header");

        QLabel* info = new QLabel("Enter amount to generate QR");
        info->setObjectName("subHeader");
        info->setAlignment(Qt::AlignCenter);

        upiAmtInput = new QLineEdit;
        upiAmtInput->setPlaceholderText("₹ Amount (Max 10,000)");
        upiAmtInput->setValidator(new QDoubleValidator(0, 10000, 2, this));
        upiAmtInput->setAlignment(Qt::AlignCenter);

        QPushButton* genQrBtn = new QPushButton("Generate QR Code");
        genQrBtn->setObjectName("upiBtn");

        QPushButton* backBtn = new QPushButton("← Back");
        backBtn->setObjectName("cancelBtn");

        connect(genQrBtn, &QPushButton::clicked, [this]() {
            double amount = upiAmtInput->text().toDouble();
            if (amount <= 0 || amount > 10000 || amount % 100 != 0) {
                QMessageBox::warning(this, "Invalid", "Please enter a valid amount.");
                return;
            }
            generateAndShowQR(amount);
        });

        connect(backBtn, &QPushButton::clicked, [this]() {
            stackedLayout->setCurrentIndex(0);
        });

        cardLayout->addWidget(header);
        cardLayout->addWidget(info);
        cardLayout->addSpacing(20);
        cardLayout->addWidget(upiAmtInput);
        cardLayout->addSpacing(20);
        cardLayout->addWidget(genQrBtn);
        cardLayout->addWidget(backBtn);

        pageLayout->addWidget(card);
    }

    // --- Page 3: QR Display & Timer ---
    void createQRDisplayPage() {
        qrDisplayPage = new QWidget;
        QVBoxLayout* pageLayout = new QVBoxLayout(qrDisplayPage);
        pageLayout->setAlignment(Qt::AlignCenter);

        QVBoxLayout* cardLayout;
        QFrame* card = createPageContainer(cardLayout);

        QLabel* info = new QLabel("SCAN TO PAY");
        info->setObjectName("header");

        qrImageLabel = new QLabel;
        qrImageLabel->setAlignment(Qt::AlignCenter);
        qrImageLabel->setFixedSize(300, 300);
        qrImageLabel->setStyleSheet("background-color: white; border-radius: 10px; border: 5px solid white;");

        timerLabel = new QLabel("05:00");
        timerLabel->setObjectName("timer");
        timerLabel->setAlignment(Qt::AlignCenter);

        QLabel* hint = new QLabel("Use any UPI App to scan");
        hint->setObjectName("subHeader");
        hint->setAlignment(Qt::AlignCenter);

        QPushButton* cancelQrBtn = new QPushButton("Cancel Transaction");
        cancelQrBtn->setObjectName("cancelBtn");

        connect(cancelQrBtn, &QPushButton::clicked, [this]() {
            stopUPISession();
        });

        cardLayout->addWidget(info);
        cardLayout->addWidget(qrImageLabel, 0, Qt::AlignCenter);
        cardLayout->addWidget(timerLabel);
        cardLayout->addWidget(hint);
        cardLayout->addSpacing(10);
        cardLayout->addWidget(cancelQrBtn);

        pageLayout->addWidget(card);
    }

    // --- Helper Functions ---
    void resetLoginUI() {
        pendingAccount = nullptr;
        accInput->clear();
        pinInput->clear();
        titleLabel->setText("BANK ATM");

        accInput->setVisible(true);
        verifyAccBtn->setVisible(true);
        upiLoginBtn->setVisible(true);
        nfcLoginBtn->setVisible(true);

        nfcCardImage->setVisible(false);

        userLabel->setVisible(false);
        pinInput->setVisible(false);
        loginBtn->setVisible(false);
        cancelBtn->setVisible(false);
        nfcStatusLabel->setVisible(false);
    }

    void refreshDashboard() {
        if (!currentSession) return;
        welcomeLabel->setText("Hello, " + currentSession->getName());
        balanceLabel->setText("₹" + QString::number(currentSession->getBalance(), 'f', 2));
    }

    void generateAndShowQR(double amount) {
        QString upiString = QString("upi://pay?pa=atm@bank&pn=ATM%20Machine%20Simulation&am=%1&cu=INR")
        .arg(amount);

        try {
            QrCode qr = QrCode::encodeText(upiString.toUtf8().constData(), QrCode::Ecc::LOW);
            int size = qr.getSize();
            int scale = 7;
            int border = 2;
            int imgSize = (size + border * 2) * scale;

            QImage image(imgSize, imgSize, QImage::Format_RGB32);
            image.fill(Qt::white);

            QPainter painter(&image);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::black);

            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    if (qr.getModule(x, y)) {
                        painter.drawRect((x + border) * scale, (y + border) * scale, scale, scale);
                    }
                }
            }
            qrImageLabel->setPixmap(QPixmap::fromImage(image));
            timeLeft = 300;
            updateTimerLabel();
            upiTimer->start(1000);
            stackedLayout->setCurrentIndex(3);
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "QR Error", e.what());
        }
    }

    void updateTimer() {
        timeLeft--;
        updateTimerLabel();
        if (timeLeft <= 0) {
            stopUPISession();
            QMessageBox::information(this, "Timeout", "Transaction timed out.");
        }
    }

    void updateTimerLabel() {
        int min = timeLeft / 60;
        int sec = timeLeft % 60;
        timerLabel->setText(QString("%1:%2")
                                .arg(min, 2, 10, QChar('0'))
                                .arg(sec, 2, 10, QChar('0')));
    }

    void stopUPISession() {
        upiTimer->stop();
        stackedLayout->setCurrentIndex(0);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    ATMWindow window;
    window.show();
    return app.exec();
}
