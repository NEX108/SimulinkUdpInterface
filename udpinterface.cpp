#include "udpinterface.h"

#include <QNetworkDatagram>

UdpInterface::UdpInterface(QObject *parent)
    : QObject(parent)
{
    connect(
        &m_socket,
        &QUdpSocket::readyRead,
        this,
        &UdpInterface::processPendingDatagrams);

    connect(
        &m_socket,
        &QUdpSocket::errorOccurred,
        this,
        &UdpInterface::handleSocketError);
}

UdpInterface::~UdpInterface()
{
    stop();
}

bool UdpInterface::startReceiver(
    quint16 receivePort,
    QString &errorMessage)
{
    errorMessage.clear();

    if (m_receiverRunning) {
        errorMessage =
            QStringLiteral(
                "Der UDP-Empfänger läuft bereits auf Port %1.")
                .arg(m_receivePort);

        return false;
    }

    if (receivePort == 0) {
        errorMessage =
            QStringLiteral(
                "Der UDP-Empfangsport darf nicht 0 sein.");

        return false;
    }

    const bool bindSuccessful =
        m_socket.bind(
            QHostAddress::AnyIPv4,
            receivePort);

    if (!bindSuccessful) {
        ++m_receiveErrors;

        errorMessage =
            QStringLiteral(
                "UDP-Port %1 konnte nicht geöffnet werden: %2")
                .arg(receivePort)
                .arg(m_socket.errorString());

        return false;
    }

    m_receivePort = receivePort;
    m_receiverRunning = true;

    m_latestPacket.clear();
    m_hasNewPacket = false;

    m_lastSenderAddress.clear();
    m_lastSenderPort = 0;

    return true;
}

bool UdpInterface::startSender(
    const QHostAddress &destinationAddress,
    quint16 destinationPort,
    QString &errorMessage)
{
    errorMessage.clear();

    if (destinationAddress.isNull()) {
        errorMessage =
            QStringLiteral(
                "Die UDP-Zieladresse ist ungültig.");

        return false;
    }

    if (destinationPort == 0) {
        errorMessage =
            QStringLiteral(
                "Der UDP-Zielport darf nicht 0 sein.");

        return false;
    }

    m_destinationAddress = destinationAddress;
    m_destinationPort = destinationPort;
    m_senderConfigured = true;

    return true;
}

void UdpInterface::stop()
{
    if (m_socket.state()
        != QAbstractSocket::UnconnectedState) {
        m_socket.close();
    }

    m_receiverRunning = false;
    m_senderConfigured = false;

    m_receivePort = 0;
    m_destinationPort = 0;
    m_destinationAddress.clear();

    m_latestPacket.clear();
    m_hasNewPacket = false;

    m_lastSenderAddress.clear();
    m_lastSenderPort = 0;
}

bool UdpInterface::sendPacket(
    const QByteArray &packet,
    QString &errorMessage)
{
    errorMessage.clear();

    if (!m_senderConfigured) {
        errorMessage =
            QStringLiteral(
                "Der UDP-Sender wurde nicht konfiguriert.");

        return false;
    }

    if (packet.isEmpty()) {
        errorMessage =
            QStringLiteral(
                "Ein leeres UDP-Paket wird nicht gesendet.");

        return false;
    }

    const qint64 bytesWritten =
        m_socket.writeDatagram(
            packet,
            m_destinationAddress,
            m_destinationPort);

    if (bytesWritten < 0) {
        ++m_sendErrors;

        errorMessage =
            QStringLiteral(
                "UDP-Paket konnte nicht gesendet werden: %1")
                .arg(m_socket.errorString());

        emit udpError(errorMessage);
        return false;
    }

    if (bytesWritten != packet.size()) {
        ++m_sendErrors;

        errorMessage =
            QStringLiteral(
                "UDP-Paket wurde nicht vollständig gesendet. "
                "Erwartet: %1 Byte, gesendet: %2 Byte.")
                .arg(packet.size())
                .arg(bytesWritten);

        emit udpError(errorMessage);
        return false;
    }

    ++m_packetsSent;

    emit packetSent(
        static_cast<qsizetype>(bytesWritten));

    return true;
}

bool UdpInterface::isReceiverRunning() const
{
    return m_receiverRunning;
}

bool UdpInterface::isSenderConfigured() const
{
    return m_senderConfigured;
}

bool UdpInterface::hasNewPacket() const
{
    return m_hasNewPacket;
}

QByteArray UdpInterface::takeLatestPacket()
{
    m_hasNewPacket = false;
    return m_latestPacket;
}

quint16 UdpInterface::receivePort() const
{
    return m_receivePort;
}

QHostAddress UdpInterface::destinationAddress() const
{
    return m_destinationAddress;
}

quint16 UdpInterface::destinationPort() const
{
    return m_destinationPort;
}

quint64 UdpInterface::packetsReceived() const
{
    return m_packetsReceived;
}

quint64 UdpInterface::packetsSent() const
{
    return m_packetsSent;
}

quint64 UdpInterface::receiveErrors() const
{
    return m_receiveErrors;
}

quint64 UdpInterface::sendErrors() const
{
    return m_sendErrors;
}

QHostAddress UdpInterface::lastSenderAddress() const
{
    return m_lastSenderAddress;
}

quint16 UdpInterface::lastSenderPort() const
{
    return m_lastSenderPort;
}

void UdpInterface::processPendingDatagrams()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram =
            m_socket.receiveDatagram();

        if (!datagram.isValid()) {
            ++m_receiveErrors;

            emit udpError(
                QStringLiteral(
                    "Ein UDP-Datagramm konnte nicht gelesen werden: %1")
                    .arg(m_socket.errorString()));

            continue;
        }

        /*
         * Nur das aktuellste Datagramm wird gespeichert.
         * Ältere Pakete werden nicht nachträglich abgearbeitet.
         */
        m_latestPacket = datagram.data();
        m_hasNewPacket = true;

        m_lastSenderAddress =
            datagram.senderAddress();

        m_lastSenderPort =
            datagram.senderPort();

        ++m_packetsReceived;

        emit packetReceived(
            m_latestPacket.size(),
            m_lastSenderAddress,
            m_lastSenderPort);
    }
}

void UdpInterface::handleSocketError(
    QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError)

    /*
     * Fehler während eines bewussten stop() werden ignoriert.
     */
    if (!m_receiverRunning
        && !m_senderConfigured) {
        return;
    }

    ++m_receiveErrors;

    emit udpError(
        QStringLiteral(
            "UDP-Socketfehler: %1")
            .arg(m_socket.errorString()));
}