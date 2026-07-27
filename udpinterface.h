#ifndef UDPINTERFACE_H
#define UDPINTERFACE_H

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>

class UdpInterface : public QObject
{
    Q_OBJECT

public:
    explicit UdpInterface(QObject *parent = nullptr);
    ~UdpInterface() override;

    bool startReceiver(
        quint16 receivePort,
        QString &errorMessage);

    bool startSender(
        const QHostAddress &destinationAddress,
        quint16 destinationPort,
        QString &errorMessage);

    void stop();

    bool sendPacket(
        const QByteArray &packet,
        QString &errorMessage);

    bool isReceiverRunning() const;
    bool isSenderConfigured() const;

    bool hasNewPacket() const;
    QByteArray takeLatestPacket();

    quint16 receivePort() const;
    QHostAddress destinationAddress() const;
    quint16 destinationPort() const;

    quint64 packetsReceived() const;
    quint64 packetsSent() const;
    quint64 receiveErrors() const;
    quint64 sendErrors() const;

    QHostAddress lastSenderAddress() const;
    quint16 lastSenderPort() const;

signals:
    void packetReceived(
        qsizetype byteCount,
        const QHostAddress &senderAddress,
        quint16 senderPort);

    void packetSent(qsizetype byteCount);
    void udpError(const QString &message);

private slots:
    void processPendingDatagrams();

    void handleSocketError(
        QAbstractSocket::SocketError socketError);

private:
    QUdpSocket m_socket;

    QHostAddress m_destinationAddress;
    quint16 m_receivePort = 0;
    quint16 m_destinationPort = 0;

    QByteArray m_latestPacket;
    bool m_hasNewPacket = false;

    bool m_receiverRunning = false;
    bool m_senderConfigured = false;

    QHostAddress m_lastSenderAddress;
    quint16 m_lastSenderPort = 0;

    quint64 m_packetsReceived = 0;
    quint64 m_packetsSent = 0;
    quint64 m_receiveErrors = 0;
    quint64 m_sendErrors = 0;
};

#endif // UDPINTERFACE_H