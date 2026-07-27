#ifndef ALGORITHMRUNTIME_H
#define ALGORITHMRUNTIME_H

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QLibrary>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QHostAddress>

#include "udpinterface.h"

struct SignalDescriptor
{
    QString name;
    QString direction;
    QString dataType;
    QString cType;
    QVector<int> dimensions;
    qsizetype elementCount = 0;
    qsizetype byteOffset = 0;
    qsizetype byteSize = 0;
};

struct SignalBinding
{
    SignalDescriptor descriptor;
    void *address = nullptr;
    bool isScalar = false;
    bool isNumeric = false;
};

struct RuntimeStatistics
{
    quint64 packetsReceived = 0;
    quint64 packetsSent = 0;
    quint64 packetsMissed = 0;
    quint64 algorithmExecutions = 0;

    double lastCycleTimeMs = 0.0;
    double lastAlgorithmTimeMs = 0.0;
};

struct UdpReceiveConfiguration
{
    bool enabled = false;
    quint16 port = 0;
};

struct UdpSendConfiguration
{
    bool enabled = false;
    QHostAddress address;
    quint16 port = 0;
};

struct UdpRuntimeConfiguration
{
    UdpReceiveConfiguration lidar;
    UdpReceiveConfiguration steering;
    UdpReceiveConfiguration motorRpm;

    UdpSendConfiguration command;

    /*
     * Zuordnung zwischen UDP-Kanälen und Simulink-Signalen.
     */
    QString lidarXInputSignal;
    QString lidarYInputSignal;
    QString steeringInputSignal;
    QString motorInputSignal;

    QString steeringOutputSignal;
    QString motorOutputSignal;
};

struct DecodedLidarData
{
    bool valid = false;

    float angleMin = 0.0f;
    float angleIncrement = 0.0f;
    float rangeMin = 0.0f;
    float rangeMax = 0.0f;

    quint16 numberOfRanges = 0;

    QVector<float> ranges;
    QVector<float> x;
    QVector<float> y;

    quint32 validCount = 0;
};

class AlgorithmRuntime : public QObject
{
    Q_OBJECT

public:
    explicit AlgorithmRuntime(QObject *parent = nullptr);
    ~AlgorithmRuntime() override;

    /*
     * Der ausgewählte Algorithmusordner muss manifest.json und
     * die im Manifest angegebene Shared Library enthalten.
     */
    void configure(const QString &algorithmFolder);

    bool validate(QStringList &errors);

    bool start(
        const UdpRuntimeConfiguration &udpConfiguration,
        QString &errorMessage);
    void stop();
    void unload();

    bool writeInputBytes(
        const QString &signalName,
        const QByteArray &data,
        QString &errorMessage);

    bool writeInputScalar(
        const QString &signalName,
        double value,
        QString &errorMessage);

    bool readOutputBytes(
        const QString &signalName,
        QByteArray &data,
        QString &errorMessage) const;

    bool readOutputScalar(
        const QString &signalName,
        double &value,
        QString &errorMessage) const;

    const QVector<SignalDescriptor> &inputs() const;
    const QVector<SignalDescriptor> &outputs() const;

    QString modelName() const;
    QString algorithmFolder() const;
    QString libraryPath() const;

    bool isLoaded() const;
    bool isRunning() const;

    quint64 stepCount() const;
    RuntimeStatistics statistics() const;

signals:
    void stepCompleted(quint64 stepCount);
    void runtimeError(const QString &message);
    void lidarDataUpdated(
        const QVector<float> &x,
        const QVector<float> &y);
    void steeringActualUpdated(float value);
    void motorRpmUpdated(float value);

private:
    using InitializeFunction = void (*)();
    using StepFunction = void (*)();
    using TerminateFunction = void (*)();

    bool loadManifest(QString &errorMessage);
    bool loadLibrary(QString &errorMessage);
    bool resolveSymbols(QStringList &errors);
    bool createSignalBindings(QStringList &errors);

    bool applyInputPacket(
        const QByteArray &packet,
        QString &errorMessage);

    bool decodeStatePacket(
        const QByteArray &packet,
        float &value,
        QString &errorMessage) const;

    bool decodeLidarPacket(
        const QByteArray &packet,
        DecodedLidarData &lidarData,
        QString &errorMessage) const;

    QByteArray createOutputPacket(
        QString &errorMessage) const;

    qsizetype expectedInputPacketSize() const;
    qsizetype expectedOutputPacketSize() const;

    bool parseSignalArray(
        const QJsonArray &array,
        const QString &expectedDirection,
        QVector<SignalDescriptor> &target,
        QString &errorMessage);

    const SignalBinding *findInputBinding(
        const QString &name) const;

    const SignalBinding *findOutputBinding(
        const QString &name) const;

    bool writeScalarToMemory(
        const SignalBinding &binding,
        double value,
        QString &errorMessage);

    bool readScalarFromMemory(
        const SignalBinding &binding,
        double &value,
        QString &errorMessage) const;

    bool isNumericType(
        const SignalDescriptor &signal) const;

    void clearResolvedSymbols();
    void resetManifestData();
    void executeCycle();

    QString m_algorithmFolder;
    QString m_manifestPath;
    QString m_libraryPath;
    QString m_modelName;

    QString m_initializeSymbol;
    QString m_stepSymbol;
    QString m_terminateSymbol;
    QString m_inputContainerSymbol;
    QString m_outputContainerSymbol;

    qsizetype m_inputContainerSize = 0;
    qsizetype m_outputContainerSize = 0;

    QVector<SignalDescriptor> m_inputs;
    QVector<SignalDescriptor> m_outputs;

    QVector<SignalBinding> m_inputBindings;
    QVector<SignalBinding> m_outputBindings;

    QHash<QString, qsizetype> m_inputBindingIndices;
    QHash<QString, qsizetype> m_outputBindingIndices;

    QLibrary m_library;
    QTimer m_timer;

    InitializeFunction m_initializeFunction = nullptr;
    StepFunction m_stepFunction = nullptr;
    TerminateFunction m_terminateFunction = nullptr;

    void *m_inputContainer = nullptr;
    void *m_outputContainer = nullptr;

    quint64 m_currentStepCount = 0;
    bool m_running = false;

    RuntimeStatistics m_statistics;
    QElapsedTimer m_cycleTimer;

    /*
     * UDP-Empfang
     */
    UdpInterface m_lidarUdp;
    UdpInterface m_steeringUdp;
    UdpInterface m_motorRpmUdp;

    /*
     * UDP-Senden
     */
    UdpInterface m_commandUdp;
    QByteArray m_latestUdpPacket;

    UdpRuntimeConfiguration m_udpConfiguration;
};

#endif // ALGORITHMRUNTIME_H
