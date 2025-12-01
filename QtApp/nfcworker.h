#ifndef NFCWORKER_H
#define NFCWORKER_H

#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <string>

// We moved the class definition here
class NfcWorker : public QThread {
    Q_OBJECT

signals:
    void cardDetected(long long cardNum);
    void errorOccurred(QString msg);

protected:
    void run() override; // Implementation is in the cpp file

private:
    std::string parseJsonValue(std::string body, std::string key);
};

#endif // NFCWORKER_H
