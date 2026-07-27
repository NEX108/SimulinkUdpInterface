#include "algorithmruntime.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{

void *resolveDataSymbol(
    const QString &libraryPath,
    const QString &symbolName)
{
#ifdef Q_OS_WIN
    const std::wstring nativePath = libraryPath.toStdWString();

    HMODULE module = LoadLibraryW(nativePath.c_str());
    if (!module) {
        return nullptr;
    }

    const QByteArray symbolBytes = symbolName.toLatin1();
    FARPROC symbol = GetProcAddress(module, symbolBytes.constData());

    void *result = reinterpret_cast<void *>(symbol);

    /*
     * QLibrary hält die Bibliothek weiterhin geladen.
     * LoadLibraryW erhöht hier nur vorübergehend den Referenzzähler.
     */
    FreeLibrary(module);
    return result;
#else
    const QByteArray nativePath = QFile::encodeName(libraryPath);

    void *handle = dlopen(
        nativePath.constData(),
        RTLD_LAZY | RTLD_LOCAL);

    if (!handle) {
        return nullptr;
    }

    const QByteArray symbolBytes = symbolName.toLatin1();
    void *result = dlsym(handle, symbolBytes.constData());

    /*
     * QLibrary hält die Bibliothek weiterhin geladen.
     */
    dlclose(handle);
    return result;
#endif
}

bool jsonInteger(
    const QJsonObject &object,
    const QString &key,
    qsizetype &value)
{
    const QJsonValue jsonValue = object.value(key);

    if (!jsonValue.isDouble()) {
        return false;
    }

    const double number = jsonValue.toDouble();

    if (number < 0.0
        || number > static_cast<double>(
               std::numeric_limits<qsizetype>::max())) {
        return false;
    }

    value = static_cast<qsizetype>(number);
    return true;
}

}

AlgorithmRuntime::AlgorithmRuntime(QObject *parent)
    : QObject(parent)
{
    /*
     * 20 ms = 50 Hz.
     * UDP-Empfang und UDP-Senden werden später in diesen Zyklus eingebunden.
     */
    m_timer.setInterval(20);
    m_timer.setTimerType(Qt::PreciseTimer);

    connect(
        &m_timer,
        &QTimer::timeout,
        this,
        &AlgorithmRuntime::executeCycle);

    auto connectReceiver =
        [this](UdpInterface &udp)
    {
        connect(
            &udp,
            &UdpInterface::udpError,
            this,
            [this](const QString &message)
            {
                emit runtimeError(message);
            });

            connect(
                &udp,
                &UdpInterface::packetReceived,
                this,
                [](qsizetype,
                   const QHostAddress &,
                   quint16)
                {
                });
    };

    connectReceiver(m_lidarUdp);
    connectReceiver(m_steeringUdp);
    connectReceiver(m_motorRpmUdp);
    connectReceiver(m_commandUdp);
}

AlgorithmRuntime::~AlgorithmRuntime()
{
    unload();
}

void AlgorithmRuntime::configure(const QString &algorithmFolder)
{
    unload();

    m_algorithmFolder =
        QDir::cleanPath(QDir(algorithmFolder).absolutePath());

    m_manifestPath =
        QDir(m_algorithmFolder).filePath(
            QStringLiteral("manifest.json"));
}

bool AlgorithmRuntime::validate(QStringList &errors)
{
    errors.clear();

    unload();

    QString errorMessage;

    if (!loadManifest(errorMessage)) {
        errors.append(errorMessage);
        return false;
    }

    if (!loadLibrary(errorMessage)) {
        errors.append(errorMessage);
        return false;
    }

    if (!resolveSymbols(errors)) {
        unload();
        return false;
    }

    if (!createSignalBindings(errors)) {
        unload();
        return false;
    }

    return true;
}

bool AlgorithmRuntime::start(
    const UdpRuntimeConfiguration &udpConfiguration,
    QString &errorMessage)
{
    errorMessage.clear();

    /*
     * Zuerst den allgemeinen Runtime-Zustand prüfen.
     * Noch keine UDP-Sockets öffnen.
     */
    if (m_running) {
        errorMessage =
            QStringLiteral(
                "Der Algorithmus läuft bereits.");
        return false;
    }

    if (!m_library.isLoaded()
        || !m_initializeFunction
        || !m_stepFunction
        || !m_terminateFunction
        || !m_inputContainer
        || !m_outputContainer) {

        errorMessage =
            QStringLiteral(
                "Der Algorithmus wurde noch nicht vollständig validiert.");
        return false;
    }

    m_udpConfiguration = udpConfiguration;

    /*
     * Hilfsfunktion für einen sauberen Abbruch:
     * Falls ein Kanal nicht gestartet werden kann,
     * werden bereits geöffnete Kanäle wieder geschlossen.
     */
    const auto closeUdpChannels = [this]()
    {
        m_lidarUdp.stop();
        m_steeringUdp.stop();
        m_motorRpmUdp.stop();
        m_commandUdp.stop();
    };

    /*
     * LiDAR-Empfänger
     */
    if (m_udpConfiguration.lidar.enabled) {
        if (!m_lidarUdp.startReceiver(
                m_udpConfiguration.lidar.port,
                errorMessage)) {

            closeUdpChannels();
            return false;
        }
    }

    /*
     * Lenkwinkel-Istwert-Empfänger
     */
    if (m_udpConfiguration.steering.enabled) {
        if (!m_steeringUdp.startReceiver(
                m_udpConfiguration.steering.port,
                errorMessage)) {

            closeUdpChannels();
            return false;
        }
    }

    /*
     * Motor-RPM-Empfänger
     */
    if (m_udpConfiguration.motorRpm.enabled) {
        if (!m_motorRpmUdp.startReceiver(
                m_udpConfiguration.motorRpm.port,
                errorMessage)) {

            closeUdpChannels();
            return false;
        }
    }

    /*
     * Gemeinsamer Sender für SteeringSoll und MotorSoll.
     */
    if (m_udpConfiguration.command.enabled) {
        if (!m_commandUdp.startSender(
                m_udpConfiguration.command.address,
                m_udpConfiguration.command.port,
                errorMessage)) {

            closeUdpChannels();
            return false;
        }
    }

    /*
     * Runtime zurücksetzen und Algorithmus starten.
     */
    m_currentStepCount = 0;
    m_statistics = RuntimeStatistics{};
    m_latestUdpPacket.clear();

    m_initializeFunction();

    m_running = true;
    m_cycleTimer.start();
    m_timer.start();

    return true;
}

void AlgorithmRuntime::stop()
{
    m_timer.stop();
    /*
     * Alle UDP-Kanäle schließen.
     */
    m_lidarUdp.stop();
    m_steeringUdp.stop();
    m_motorRpmUdp.stop();
    m_commandUdp.stop();

    m_latestUdpPacket.clear();

    if (m_running && m_terminateFunction) {
        m_terminateFunction();
    }

    m_running = false;
}

void AlgorithmRuntime::unload()
{
    stop();
    clearResolvedSymbols();

    if (m_library.isLoaded()) {
        m_library.unload();
    }

    m_currentStepCount = 0;
    m_statistics = RuntimeStatistics{};
}

bool AlgorithmRuntime::writeInputBytes(
    const QString &signalName,
    const QByteArray &data,
    QString &errorMessage)
{
    errorMessage.clear();

    const SignalBinding *binding =
        findInputBinding(signalName);

    if (!binding || !binding->address) {
        errorMessage =
            QStringLiteral("Eingangssignal \"%1\" wurde nicht gefunden.")
                .arg(signalName);
        return false;
    }

    if (data.size() != binding->descriptor.byteSize) {
        errorMessage =
            QStringLiteral(
                "Falsche Datengröße für \"%1\": erwartet %2 Byte, erhalten %3 Byte.")
                .arg(signalName)
                .arg(binding->descriptor.byteSize)
                .arg(data.size());
        return false;
    }

    std::memcpy(
        binding->address,
        data.constData(),
        static_cast<std::size_t>(
            binding->descriptor.byteSize));

    return true;
}

bool AlgorithmRuntime::writeInputScalar(
    const QString &signalName,
    double value,
    QString &errorMessage)
{
    errorMessage.clear();

    const SignalBinding *binding =
        findInputBinding(signalName);

    if (!binding || !binding->address) {
        errorMessage =
            QStringLiteral("Eingangssignal \"%1\" wurde nicht gefunden.")
                .arg(signalName);
        return false;
    }

    if (!binding->isScalar) {
        errorMessage =
            QStringLiteral(
                "Signal \"%1\" ist kein Skalar.")
                .arg(signalName);
        return false;
    }

    if (!binding->isNumeric) {
        errorMessage =
            QStringLiteral(
                "Signal \"%1\" besitzt keinen unterstützten numerischen Datentyp.")
                .arg(signalName);
        return false;
    }

    return writeScalarToMemory(
        *binding,
        value,
        errorMessage);
}

bool AlgorithmRuntime::readOutputBytes(
    const QString &signalName,
    QByteArray &data,
    QString &errorMessage) const
{
    errorMessage.clear();
    data.clear();

    const SignalBinding *binding =
        findOutputBinding(signalName);

    if (!binding || !binding->address) {
        errorMessage =
            QStringLiteral("Ausgangssignal \"%1\" wurde nicht gefunden.")
                .arg(signalName);
        return false;
    }

    data = QByteArray(
        static_cast<const char *>(binding->address),
        binding->descriptor.byteSize);

    return true;
}

bool AlgorithmRuntime::readOutputScalar(
    const QString &signalName,
    double &value,
    QString &errorMessage) const
{
    errorMessage.clear();
    value = 0.0;

    const SignalBinding *binding =
        findOutputBinding(signalName);

    if (!binding || !binding->address) {
        errorMessage =
            QStringLiteral("Ausgangssignal \"%1\" wurde nicht gefunden.")
                .arg(signalName);
        return false;
    }

    if (!binding->isScalar) {
        errorMessage =
            QStringLiteral(
                "Signal \"%1\" ist kein Skalar.")
                .arg(signalName);
        return false;
    }

    if (!binding->isNumeric) {
        errorMessage =
            QStringLiteral(
                "Signal \"%1\" besitzt keinen unterstützten numerischen Datentyp.")
                .arg(signalName);
        return false;
    }

    return readScalarFromMemory(
        *binding,
        value,
        errorMessage);
}

const QVector<SignalDescriptor> &AlgorithmRuntime::inputs() const
{
    return m_inputs;
}

const QVector<SignalDescriptor> &AlgorithmRuntime::outputs() const
{
    return m_outputs;
}

QString AlgorithmRuntime::modelName() const
{
    return m_modelName;
}

QString AlgorithmRuntime::algorithmFolder() const
{
    return m_algorithmFolder;
}

QString AlgorithmRuntime::libraryPath() const
{
    return m_libraryPath;
}

bool AlgorithmRuntime::isLoaded() const
{
    return m_library.isLoaded();
}

bool AlgorithmRuntime::isRunning() const
{
    return m_running;
}

quint64 AlgorithmRuntime::stepCount() const
{
    return m_currentStepCount;
}

RuntimeStatistics AlgorithmRuntime::statistics() const
{
    return m_statistics;
}

bool AlgorithmRuntime::loadManifest(QString &errorMessage)
{
    errorMessage.clear();
    resetManifestData();

    if (m_algorithmFolder.isEmpty()) {
        errorMessage =
            QStringLiteral("Es wurde kein Algorithmusordner ausgewählt.");
        return false;
    }

    QFile manifestFile(m_manifestPath);

    if (!manifestFile.exists()) {
        errorMessage =
            QStringLiteral("manifest.json wurde im Algorithmusordner nicht gefunden.");
        return false;
    }

    if (!manifestFile.open(QIODevice::ReadOnly)) {
        errorMessage =
            QStringLiteral("manifest.json konnte nicht geöffnet werden: %1")
                .arg(manifestFile.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            manifestFile.readAll(),
            &parseError);

    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        errorMessage =
            QStringLiteral("manifest.json ist ungültig: %1")
                .arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();

    m_modelName =
        root.value(QStringLiteral("modelName")).toString();

    if (m_modelName.isEmpty()) {
        errorMessage =
            QStringLiteral("Im Manifest fehlt modelName.");
        return false;
    }

    const QJsonObject libraryObject =
        root.value(QStringLiteral("library")).toObject();

    const QString libraryFileName =
        libraryObject.value(
                         QStringLiteral("fileName")).toString();

    if (libraryFileName.isEmpty()) {
        errorMessage =
            QStringLiteral("Im Manifest fehlt library.fileName.");
        return false;
    }

    /*
     * Keine absoluten Pfade aus dem Manifest übernehmen.
     */
    if (QDir::isAbsolutePath(libraryFileName)) {
        errorMessage =
            QStringLiteral(
                "library.fileName darf kein absoluter Pfad sein.");
        return false;
    }

    m_libraryPath =
        QDir(m_algorithmFolder).filePath(libraryFileName);

    const QJsonObject entryPoints =
        libraryObject.value(
                         QStringLiteral("entryPoints")).toObject();

    m_initializeSymbol =
        entryPoints.value(
                       QStringLiteral("initialize")).toString();

    m_stepSymbol =
        entryPoints.value(
                       QStringLiteral("step")).toString();

    m_terminateSymbol =
        entryPoints.value(
                       QStringLiteral("terminate")).toString();

    if (m_initializeSymbol.isEmpty()
        || m_stepSymbol.isEmpty()
        || m_terminateSymbol.isEmpty()) {
        errorMessage =
            QStringLiteral(
                "Im Manifest fehlen Einträge unter library.entryPoints.");
        return false;
    }

    const QJsonObject containers =
        root.value(QStringLiteral("containers")).toObject();

    const QJsonObject inputContainer =
        containers.value(QStringLiteral("input")).toObject();

    const QJsonObject outputContainer =
        containers.value(QStringLiteral("output")).toObject();

    m_inputContainerSymbol =
        inputContainer.value(
                          QStringLiteral("symbol")).toString();

    m_outputContainerSymbol =
        outputContainer.value(
                           QStringLiteral("symbol")).toString();

    if (m_inputContainerSymbol.isEmpty()
        || m_outputContainerSymbol.isEmpty()
        || !jsonInteger(
            inputContainer,
            QStringLiteral("byteSize"),
            m_inputContainerSize)
        || !jsonInteger(
            outputContainer,
            QStringLiteral("byteSize"),
            m_outputContainerSize)) {
        errorMessage =
            QStringLiteral(
                "Die Containerbeschreibung im Manifest ist unvollständig.");
        return false;
    }

    if (!parseSignalArray(
            root.value(QStringLiteral("inputs")).toArray(),
            QStringLiteral("input"),
            m_inputs,
            errorMessage)) {
        return false;
    }

    if (!parseSignalArray(
            root.value(QStringLiteral("outputs")).toArray(),
            QStringLiteral("output"),
            m_outputs,
            errorMessage)) {
        return false;
    }

    return true;
}

bool AlgorithmRuntime::loadLibrary(QString &errorMessage)
{
    errorMessage.clear();

    if (!QFile::exists(m_libraryPath)) {
        errorMessage =
            QStringLiteral("Bibliothek wurde nicht gefunden: %1")
                .arg(m_libraryPath);
        return false;
    }

    if (m_library.isLoaded()) {
        m_library.unload();
    }

    m_library.setFileName(m_libraryPath);

    if (!m_library.load()) {
        errorMessage =
            QStringLiteral("Bibliothek konnte nicht geladen werden: %1")
                .arg(m_library.errorString());
        return false;
    }

    return true;
}

bool AlgorithmRuntime::resolveSymbols(QStringList &errors)
{
    clearResolvedSymbols();

    m_initializeFunction =
        reinterpret_cast<InitializeFunction>(
            m_library.resolve(
                m_initializeSymbol.toLatin1().constData()));

    if (!m_initializeFunction) {
        errors.append(
            QStringLiteral("Funktion %1 wurde nicht gefunden.")
                .arg(m_initializeSymbol));
    }

    m_stepFunction =
        reinterpret_cast<StepFunction>(
            m_library.resolve(
                m_stepSymbol.toLatin1().constData()));

    if (!m_stepFunction) {
        errors.append(
            QStringLiteral("Funktion %1 wurde nicht gefunden.")
                .arg(m_stepSymbol));
    }

    m_terminateFunction =
        reinterpret_cast<TerminateFunction>(
            m_library.resolve(
                m_terminateSymbol.toLatin1().constData()));

    if (!m_terminateFunction) {
        errors.append(
            QStringLiteral("Funktion %1 wurde nicht gefunden.")
                .arg(m_terminateSymbol));
    }

    m_inputContainer =
        resolveDataSymbol(
            m_libraryPath,
            m_inputContainerSymbol);

    if (!m_inputContainer) {
        errors.append(
            QStringLiteral("Container %1 wurde nicht gefunden.")
                .arg(m_inputContainerSymbol));
    }

    m_outputContainer =
        resolveDataSymbol(
            m_libraryPath,
            m_outputContainerSymbol);

    if (!m_outputContainer) {
        errors.append(
            QStringLiteral("Container %1 wurde nicht gefunden.")
                .arg(m_outputContainerSymbol));
    }

    return errors.isEmpty();
}


bool AlgorithmRuntime::createSignalBindings(QStringList &errors)
{
    m_inputBindings.clear();
    m_outputBindings.clear();
    m_inputBindingIndices.clear();
    m_outputBindingIndices.clear();

    if (!m_inputContainer || !m_outputContainer) {
        errors.append(
            QStringLiteral(
                "Signalbindungen können ohne gültige Container nicht erstellt werden.")
            );
        return false;
    }

    m_inputBindings.reserve(m_inputs.size());
    m_outputBindings.reserve(m_outputs.size());

    for (const SignalDescriptor &descriptor : m_inputs) {
        if (m_inputBindingIndices.contains(descriptor.name)) {
            errors.append(
                QStringLiteral(
                    "Eingangssignal \"%1\" ist im Manifest mehrfach vorhanden.")
                    .arg(descriptor.name)
                );
            continue;
        }

        SignalBinding binding;
        binding.descriptor = descriptor;
        binding.address =
            static_cast<unsigned char *>(m_inputContainer)
            + descriptor.byteOffset;
        binding.isScalar = descriptor.elementCount == 1;
        binding.isNumeric = isNumericType(descriptor);

        const qsizetype index = m_inputBindings.size();
        m_inputBindings.append(binding);
        m_inputBindingIndices.insert(descriptor.name, index);
    }

    for (const SignalDescriptor &descriptor : m_outputs) {
        if (m_outputBindingIndices.contains(descriptor.name)) {
            errors.append(
                QStringLiteral(
                    "Ausgangssignal \"%1\" ist im Manifest mehrfach vorhanden.")
                    .arg(descriptor.name)
                );
            continue;
        }

        SignalBinding binding;
        binding.descriptor = descriptor;
        binding.address =
            static_cast<unsigned char *>(m_outputContainer)
            + descriptor.byteOffset;
        binding.isScalar = descriptor.elementCount == 1;
        binding.isNumeric = isNumericType(descriptor);

        const qsizetype index = m_outputBindings.size();
        m_outputBindings.append(binding);
        m_outputBindingIndices.insert(descriptor.name, index);
    }

    return errors.isEmpty();
}


bool AlgorithmRuntime::parseSignalArray(
    const QJsonArray &array,
    const QString &expectedDirection,
    QVector<SignalDescriptor> &target,
    QString &errorMessage)
{
    target.clear();

    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            errorMessage =
                QStringLiteral("Signalbeschreibung %1 ist kein JSON-Objekt.")
                    .arg(index);
            return false;
        }

        const QJsonObject object = array.at(index).toObject();

        SignalDescriptor signal;
        signal.name =
            object.value(QStringLiteral("name")).toString();

        signal.direction =
            object.value(QStringLiteral("direction")).toString();

        signal.dataType =
            object.value(QStringLiteral("dataType")).toString();

        signal.cType =
            object.value(QStringLiteral("cType")).toString();

        qsizetype elementCount = 0;
        qsizetype byteOffset = 0;
        qsizetype byteSize = 0;

        if (signal.name.isEmpty()
            || signal.direction != expectedDirection
            || signal.dataType.isEmpty()
            || !jsonInteger(
                object,
                QStringLiteral("elementCount"),
                elementCount)
            || !jsonInteger(
                object,
                QStringLiteral("byteOffset"),
                byteOffset)
            || !jsonInteger(
                object,
                QStringLiteral("byteSize"),
                byteSize)) {
            errorMessage =
                QStringLiteral("Signalbeschreibung %1 ist unvollständig.")
                    .arg(index);
            return false;
        }

        signal.elementCount = elementCount;
        signal.byteOffset = byteOffset;
        signal.byteSize = byteSize;

        const QJsonArray dimensions =
            object.value(QStringLiteral("dimensions")).toArray();

        for (const QJsonValue &dimension : dimensions) {
            if (!dimension.isDouble()
                || dimension.toInt() < 0) {
                errorMessage =
                    QStringLiteral(
                        "Ungültige Dimension bei Signal \"%1\".")
                        .arg(signal.name);
                return false;
            }

            signal.dimensions.append(dimension.toInt());
        }

        const qsizetype containerSize =
            expectedDirection == QStringLiteral("input")
                ? m_inputContainerSize
                : m_outputContainerSize;

        if (signal.byteOffset > containerSize
            || signal.byteSize > containerSize - signal.byteOffset) {
            errorMessage =
                QStringLiteral(
                    "Signal \"%1\" liegt außerhalb seines Containers.")
                    .arg(signal.name);
            return false;
        }

        target.append(signal);
    }

    return true;
}

const SignalBinding *AlgorithmRuntime::findInputBinding(
    const QString &name) const
{
    const auto iterator =
        m_inputBindingIndices.constFind(name);

    if (iterator == m_inputBindingIndices.constEnd()) {
        return nullptr;
    }

    return &m_inputBindings.at(iterator.value());
}

const SignalBinding *AlgorithmRuntime::findOutputBinding(
    const QString &name) const
{
    const auto iterator =
        m_outputBindingIndices.constFind(name);

    if (iterator == m_outputBindingIndices.constEnd()) {
        return nullptr;
    }

    return &m_outputBindings.at(iterator.value());
}

bool AlgorithmRuntime::writeScalarToMemory(
    const SignalBinding &binding,
    double value,
    QString &errorMessage)
{
    auto *destination =
        static_cast<unsigned char *>(binding.address);

    const SignalDescriptor &signal =
        binding.descriptor;

    if (signal.dataType == QStringLiteral("single")
        || signal.cType == QStringLiteral("real32_T")) {
        const float converted = static_cast<float>(value);
        std::memcpy(destination, &converted, sizeof(converted));
        return true;
    }

    if (signal.dataType == QStringLiteral("double")
        || signal.cType == QStringLiteral("real_T")) {
        const double converted = value;
        std::memcpy(destination, &converted, sizeof(converted));
        return true;
    }

    if (signal.dataType == QStringLiteral("int8")
        || signal.cType == QStringLiteral("int8_T")) {
        const qint8 converted = static_cast<qint8>(value);
        std::memcpy(destination, &converted, sizeof(converted));
        return true;
    }

    if (signal.dataType == QStringLiteral("uint8")
        || signal.cType == QStringLiteral("uint8_T")) {
        const quint8 converted = static_cast<quint8>(value);
        std::memcpy(destination, &converted, sizeof(converted));
        return true;
    }

    if (signal.dataType == QStringLiteral("int16")
        || signal.cType == QStringLiteral("int16_T")) {
        const qint16 converted = static_cast<qint16>(value);
        std::memcpy(destination, &converted, sizeof(converted));
        return true;
    }

    if (signal.dataType == QStringLiteral("uint16")
        || signal.cType == QStringLiteral("uint16_T")) {
        const quint16 converted = static_cast<quint16>(value);
        std::memcpy(destination, &converted, sizeof(converted));
        return true;
    }

    if (signal.dataType == QStringLiteral("int32")
        || signal.cType == QStringLiteral("int32_T")) {
        const qint32 converted = static_cast<qint32>(value);
        std::memcpy(destination, &converted, sizeof(converted));
        return true;
    }

    if (signal.dataType == QStringLiteral("uint32")
        || signal.cType == QStringLiteral("uint32_T")) {
        const quint32 converted = static_cast<quint32>(value);
        std::memcpy(destination, &converted, sizeof(converted));
        return true;
    }

    if (signal.dataType == QStringLiteral("boolean")
        || signal.cType == QStringLiteral("boolean_T")) {
        const bool converted = value != 0.0;
        std::memcpy(destination, &converted, sizeof(converted));
        return true;
    }

    errorMessage =
        QStringLiteral(
            "Datentyp \"%1\" von Signal \"%2\" wird noch nicht als Skalar unterstützt.")
            .arg(signal.dataType, signal.name);

    return false;
}

bool AlgorithmRuntime::readScalarFromMemory(
    const SignalBinding &binding,
    double &value,
    QString &errorMessage) const
{
    const auto *source =
        static_cast<const unsigned char *>(binding.address);

    const SignalDescriptor &signal =
        binding.descriptor;

    if (signal.dataType == QStringLiteral("single")
        || signal.cType == QStringLiteral("real32_T")) {
        float converted = 0.0f;
        std::memcpy(&converted, source, sizeof(converted));
        value = static_cast<double>(converted);
        return true;
    }

    if (signal.dataType == QStringLiteral("double")
        || signal.cType == QStringLiteral("real_T")) {
        double converted = 0.0;
        std::memcpy(&converted, source, sizeof(converted));
        value = converted;
        return true;
    }

    if (signal.dataType == QStringLiteral("int8")
        || signal.cType == QStringLiteral("int8_T")) {
        qint8 converted = 0;
        std::memcpy(&converted, source, sizeof(converted));
        value = converted;
        return true;
    }

    if (signal.dataType == QStringLiteral("uint8")
        || signal.cType == QStringLiteral("uint8_T")) {
        quint8 converted = 0;
        std::memcpy(&converted, source, sizeof(converted));
        value = converted;
        return true;
    }

    if (signal.dataType == QStringLiteral("int16")
        || signal.cType == QStringLiteral("int16_T")) {
        qint16 converted = 0;
        std::memcpy(&converted, source, sizeof(converted));
        value = converted;
        return true;
    }

    if (signal.dataType == QStringLiteral("uint16")
        || signal.cType == QStringLiteral("uint16_T")) {
        quint16 converted = 0;
        std::memcpy(&converted, source, sizeof(converted));
        value = converted;
        return true;
    }

    if (signal.dataType == QStringLiteral("int32")
        || signal.cType == QStringLiteral("int32_T")) {
        qint32 converted = 0;
        std::memcpy(&converted, source, sizeof(converted));
        value = converted;
        return true;
    }

    if (signal.dataType == QStringLiteral("uint32")
        || signal.cType == QStringLiteral("uint32_T")) {
        quint32 converted = 0;
        std::memcpy(&converted, source, sizeof(converted));
        value = converted;
        return true;
    }

    if (signal.dataType == QStringLiteral("boolean")
        || signal.cType == QStringLiteral("boolean_T")) {
        bool converted = false;
        std::memcpy(&converted, source, sizeof(converted));
        value = converted ? 1.0 : 0.0;
        return true;
    }

    errorMessage =
        QStringLiteral(
            "Datentyp \"%1\" von Signal \"%2\" wird noch nicht als Skalar unterstützt.")
            .arg(signal.dataType, signal.name);

    return false;
}

bool AlgorithmRuntime::isNumericType(
    const SignalDescriptor &signal) const
{
    return signal.dataType == QStringLiteral("single")
    || signal.cType == QStringLiteral("real32_T")
        || signal.dataType == QStringLiteral("double")
        || signal.cType == QStringLiteral("real_T")
        || signal.dataType == QStringLiteral("int8")
        || signal.cType == QStringLiteral("int8_T")
        || signal.dataType == QStringLiteral("uint8")
        || signal.cType == QStringLiteral("uint8_T")
        || signal.dataType == QStringLiteral("int16")
        || signal.cType == QStringLiteral("int16_T")
        || signal.dataType == QStringLiteral("uint16")
        || signal.cType == QStringLiteral("uint16_T")
        || signal.dataType == QStringLiteral("int32")
        || signal.cType == QStringLiteral("int32_T")
        || signal.dataType == QStringLiteral("uint32")
        || signal.cType == QStringLiteral("uint32_T")
        || signal.dataType == QStringLiteral("boolean")
        || signal.cType == QStringLiteral("boolean_T");
}


void AlgorithmRuntime::clearResolvedSymbols()
{
    m_inputBindings.clear();
    m_outputBindings.clear();
    m_inputBindingIndices.clear();
    m_outputBindingIndices.clear();

    m_initializeFunction = nullptr;
    m_stepFunction = nullptr;
    m_terminateFunction = nullptr;
    m_inputContainer = nullptr;
    m_outputContainer = nullptr;
}

void AlgorithmRuntime::resetManifestData()
{
    m_libraryPath.clear();
    m_modelName.clear();

    m_initializeSymbol.clear();
    m_stepSymbol.clear();
    m_terminateSymbol.clear();
    m_inputContainerSymbol.clear();
    m_outputContainerSymbol.clear();

    m_inputContainerSize = 0;
    m_outputContainerSize = 0;

    m_inputs.clear();
    m_outputs.clear();
}

QByteArray AlgorithmRuntime::createOutputPacket(
    QString &errorMessage) const
{
    errorMessage.clear();

    double steeringSetpoint = 0.0;
    double motorSetpoint = 0.0;

    if (!readOutputScalar(
            m_udpConfiguration.steeringOutputSignal,
            steeringSetpoint,
            errorMessage)) {

        errorMessage =
            QStringLiteral(
                "Lenkwinkel-Soll konnte für das "
                "UDP-Paket nicht gelesen werden: %1")
                .arg(errorMessage);

        return QByteArray{};
    }

    errorMessage.clear();

    if (!readOutputScalar(
            m_udpConfiguration.motorOutputSignal,
            motorSetpoint,
            errorMessage)) {

        errorMessage =
            QStringLiteral(
                "Motor-Soll konnte für das "
                "UDP-Paket nicht gelesen werden: %1")
                .arg(errorMessage);

        return QByteArray{};
    }

    /*
     * Werte auf den normierten Steuerbereich begrenzen.
     */
    const double limitedSteering =
        std::clamp(
            steeringSetpoint,
            -1.0,
            1.0);

    const double limitedMotor =
        std::clamp(
            motorSetpoint,
            -1.0,
            1.0);

    constexpr int packetSize = 32;

    QByteArray packet(
        packetSize,
        '\0');

    const QByteArray command =
        QStringLiteral(
            "CTRL,%1,%2,%3")
            .arg(
                limitedSteering,
                0,
                'f',
                3)
            .arg(
                limitedMotor,
                0,
                'f',
                3)
            .arg(
                0.0,
                0,
                'f',
                3)
            .toLatin1();

    const qsizetype copySize =
        std::min(
            static_cast<qsizetype>(packetSize),
            command.size());

    std::memcpy(
        packet.data(),
        command.constData(),
        static_cast<std::size_t>(copySize));

    return packet;
}

qsizetype AlgorithmRuntime::expectedOutputPacketSize() const
{
    qsizetype totalSize = 0;

    for (const SignalBinding &binding :
         m_outputBindings) {
        totalSize += binding.descriptor.byteSize;
    }

    return totalSize;
}

bool AlgorithmRuntime::decodeLidarPacket(
    const QByteArray &packet,
    DecodedLidarData &lidarData,
    QString &errorMessage) const
{
    errorMessage.clear();
    lidarData = DecodedLidarData{};

    constexpr qsizetype numberOfRanges = 1601;
    constexpr qsizetype headerFloats = 8;
    constexpr qsizetype totalFloats =
        headerFloats + numberOfRanges;

    constexpr qsizetype expectedPacketSize =
        totalFloats * sizeof(float);

    if (packet.size() != expectedPacketSize) {
        errorMessage =
            QStringLiteral(
                "Ungültige LiDAR-Paketgröße: "
                "%1 Byte erwartet, %2 Byte erhalten.")
                .arg(expectedPacketSize)
                .arg(packet.size());

        return false;
    }

    static_assert(
        sizeof(float) == sizeof(quint32),
        "float muss 32 Bit groß sein.");

    auto readFloatLittleEndian =
        [&packet](qsizetype floatIndex) -> float
    {
        const qsizetype byteOffset =
            floatIndex * sizeof(float);

        const auto *address =
            reinterpret_cast<const uchar *>(
                packet.constData() + byteOffset);

        const quint32 rawValue =
            qFromLittleEndian<quint32>(address);

        float value = 0.0f;

        std::memcpy(
            &value,
            &rawValue,
            sizeof(value));

        return value;
    };

    /*
     * Header analog zu Simulink:
     *
     * values(1) = aktuell unbenutzt
     * values(2) = angle_min
     * values(3) = angle_inc
     * values(4) = range_min
     * values(5) = range_max
     * values(6) = scan_time
     * values(7) = time_inc
     * values(8) = num_ranges
     *
     * C++ verwendet nullbasierte Indizes.
     */
    lidarData.angleMin =
        readFloatLittleEndian(1);

    lidarData.angleIncrement =
        readFloatLittleEndian(2);

    lidarData.rangeMin =
        readFloatLittleEndian(3);

    lidarData.rangeMax =
        readFloatLittleEndian(4);

    const float numberOfRangesFloat =
        readFloatLittleEndian(7);

    if (!std::isfinite(numberOfRangesFloat)
        || numberOfRangesFloat < 0.0f) {

        errorMessage =
            QStringLiteral(
                "Ungültige Anzahl von LiDAR-Messwerten.");

        return false;
    }

    int decodedNumberOfRanges =
        static_cast<int>(
            std::lround(numberOfRangesFloat));

    decodedNumberOfRanges =
        std::clamp(
            decodedNumberOfRanges,
            0,
            static_cast<int>(numberOfRanges));

    lidarData.numberOfRanges =
        static_cast<quint16>(
            decodedNumberOfRanges);

    if (lidarData.numberOfRanges == 0) {
        errorMessage =
            QStringLiteral(
                "LiDAR-Paket enthält keine Messwerte.");

        return false;
    }

    if (!std::isfinite(lidarData.angleMin)
        || !std::isfinite(lidarData.angleIncrement)
        || !std::isfinite(lidarData.rangeMin)
        || !std::isfinite(lidarData.rangeMax)) {

        errorMessage =
            QStringLiteral(
                "LiDAR-Header enthält ungültige Werte.");

        return false;
    }

    if (lidarData.rangeMax <= lidarData.rangeMin) {
        errorMessage =
            QStringLiteral(
                "Ungültiger LiDAR-Messbereich: "
                "range_max muss größer als range_min sein.");

        return false;
    }

    lidarData.ranges.resize(numberOfRanges);
    lidarData.x.resize(numberOfRanges);
    lidarData.y.resize(numberOfRanges);

    const float nanValue =
        std::numeric_limits<float>::quiet_NaN();

    lidarData.validCount = 0;

    for (qsizetype i = 0;
         i < numberOfRanges;
         ++i) {

        const float range =
            readFloatLittleEndian(
                headerFloats + i);

        lidarData.ranges[i] = range;
        lidarData.x[i] = nanValue;
        lidarData.y[i] = nanValue;

        /*
         * Entspricht:
         *
         * if uint32(i) <= N
         *
         * in MATLAB bei einsbasierter Indizierung.
         */
        if (i >= lidarData.numberOfRanges) {
            continue;
        }

        if (!std::isfinite(range)
            || range < lidarData.rangeMin
            || range > lidarData.rangeMax) {

            continue;
        }

        const float angle =
            lidarData.angleMin
            + static_cast<float>(i)
                  * lidarData.angleIncrement;

        lidarData.x[i] =
            range * std::cos(angle);

        lidarData.y[i] =
            range * std::sin(angle);

        ++lidarData.validCount;
    }

    lidarData.valid = true;

    return true;
}

bool AlgorithmRuntime::decodeStatePacket(
    const QByteArray &packet,
    float &value,
    QString &errorMessage) const
{
    errorMessage.clear();
    value = 0.0f;

    /*
     * Aufbau des STAT-Pakets:
     *
     * Byte 0 ... 3   Magic "STAT"
     * Byte 4 ... 11  Version, Anzahl und Zeitstempel
     * Byte 12 ... 15 erster float32-Wert
     */
    constexpr qsizetype minimumPacketSize = 16;
    constexpr qsizetype payloadOffset = 12;

    if (packet.size() < minimumPacketSize) {
        errorMessage =
            QStringLiteral(
                "STAT-Paket ist zu klein: "
                "mindestens %1 Byte erwartet, %2 Byte erhalten.")
                .arg(minimumPacketSize)
                .arg(packet.size());

        return false;
    }

    if (packet.left(4) != QByteArrayLiteral("STAT")) {
        errorMessage =
            QStringLiteral(
                "Ungültiges State-Paket: "
                "Magic 'STAT' wurde nicht gefunden.");

        return false;
    }

    /*
     * Das UE5-/Simulink-Protokoll verwendet Little Endian.
     * Zuerst die vier Bytes als uint32 lesen und danach
     * unverändert als float32 interpretieren.
     */
    const auto *payload =
        reinterpret_cast<const uchar *>(
            packet.constData() + payloadOffset);

    const quint32 rawValue =
        qFromLittleEndian<quint32>(payload);

    static_assert(
        sizeof(float) == sizeof(quint32),
        "float muss 32 Bit groß sein.");

    std::memcpy(
        &value,
        &rawValue,
        sizeof(value));

    return true;
}

void AlgorithmRuntime::executeCycle()
{
    if (!m_running || !m_stepFunction) {
        return;
    }

    const qint64 cycleStartNanoseconds =
        m_cycleTimer.isValid()
            ? m_cycleTimer.nsecsElapsed()
            : 0;

    /*
     * Neues Eingangspaket übernehmen.
     *
     * Wenn kein Paket vorhanden ist, bleiben die vorherigen
     * Eingangswerte im Simulink-Eingangscontainer erhalten.
     */
    if (m_lidarUdp.hasNewPacket()) {
        const QByteArray packet =
            m_lidarUdp.takeLatestPacket();

        DecodedLidarData lidarData;
        QString decodeError;

        if (decodeLidarPacket(
                packet,
                lidarData,
                decodeError)) {

            ++m_statistics.packetsReceived;

            qDebug()
                << "LiDAR XY berechnet:"
                << lidarData.validCount
                << "gültige Punkte von"
                << lidarData.numberOfRanges
                << "| erster Punkt:"
                << lidarData.x.first()
                << lidarData.y.first()
                << "| Mitte:"
                << lidarData.x[800]
                << lidarData.y[800]
                << "| letzter:"
                << lidarData.x.last()
                << lidarData.y.last();

            qDebug()
                << "angleMin =" << lidarData.angleMin
                << "angleInc =" << lidarData.angleIncrement
                << "rangeMin =" << lidarData.rangeMin
                << "rangeMax =" << lidarData.rangeMax
                << "numRanges =" << lidarData.numberOfRanges;

            emit lidarDataUpdated(
                lidarData.x,
                lidarData.y);

            QString lidarWriteError;

            /*
             * QVector<float> als Rohdaten an die Simulink-Eingänge
             * übertragen. Die ausgewählten Eingangssignale müssen
             * Single-Arrays mit passender Größe sein.
             */
            const QByteArray lidarXBytes(
                reinterpret_cast<const char *>(
                    lidarData.x.constData()),
                static_cast<qsizetype>(
                    lidarData.x.size() * sizeof(float))
                );

            if (!writeInputBytes(
                    m_udpConfiguration.lidarXInputSignal,
                    lidarXBytes,
                    lidarWriteError)) {

                emit runtimeError(
                    QStringLiteral(
                        "LiDAR-X konnte nicht in den Algorithmus "
                        "geschrieben werden: %1")
                        .arg(lidarWriteError));
            }

            lidarWriteError.clear();

            const QByteArray lidarYBytes(
                reinterpret_cast<const char *>(
                    lidarData.y.constData()),
                static_cast<qsizetype>(
                    lidarData.y.size() * sizeof(float))
                );

            if (!writeInputBytes(
                    m_udpConfiguration.lidarYInputSignal,
                    lidarYBytes,
                    lidarWriteError)) {

                emit runtimeError(
                    QStringLiteral(
                        "LiDAR-Y konnte nicht in den Algorithmus "
                        "geschrieben werden: %1")
                        .arg(lidarWriteError));
            }
        }
        else {
            emit runtimeError(
                QStringLiteral("LiDAR: %1")
                    .arg(decodeError));
        }
    }

    if (m_steeringUdp.hasNewPacket()) {
        const QByteArray packet =
            m_steeringUdp.takeLatestPacket();

        float steeringActual = 0.0f;
        QString decodeError;

        if (decodeStatePacket(
                packet,
                steeringActual,
                decodeError)) {

            QString writeError;

            if (writeInputScalar(
                    m_udpConfiguration.steeringInputSignal,
                    static_cast<double>(steeringActual),
                    writeError)) {

                ++m_statistics.packetsReceived;

                emit steeringActualUpdated(steeringActual);

                qDebug()
                    << "Lenkwinkel Ist:"
                    << steeringActual
                    << "→"
                    << m_udpConfiguration.steeringInputSignal;
            }
            else {
                emit runtimeError(
                    QStringLiteral(
                        "Lenkwinkel konnte nicht in den "
                        "Algorithmus geschrieben werden: %1")
                        .arg(writeError));
            }
        }
        else {
            emit runtimeError(
                QStringLiteral("Lenkwinkel: %1")
                    .arg(decodeError));
        }
    }

    if (m_motorRpmUdp.hasNewPacket()) {
        const QByteArray packet =
            m_motorRpmUdp.takeLatestPacket();

        float motorRpm = 0.0f;
        QString decodeError;

        if (decodeStatePacket(
                packet,
                motorRpm,
                decodeError)) {

            QString writeError;

            if (writeInputScalar(
                    m_udpConfiguration.motorInputSignal,
                    static_cast<double>(motorRpm),
                    writeError)) {

                ++m_statistics.packetsReceived;

                emit motorRpmUpdated(motorRpm);

                qDebug()
                    << "Motor RPM:"
                    << motorRpm
                    << "→"
                    << m_udpConfiguration.motorInputSignal;
            }
            else {
                emit runtimeError(
                    QStringLiteral(
                        "Motor-RPM konnte nicht in den "
                        "Algorithmus geschrieben werden: %1")
                        .arg(writeError));
            }
        }
        else {
            emit runtimeError(
                QStringLiteral("Motor RPM: %1")
                    .arg(decodeError));
        }
    }

    /*
     * Algorithmus einmal ausführen.
     */
    QElapsedTimer algorithmTimer;
    algorithmTimer.start();

    m_stepFunction();

    m_statistics.lastAlgorithmTimeMs =
        static_cast<double>(
            algorithmTimer.nsecsElapsed())
        / 1'000'000.0;

    ++m_currentStepCount;
    ++m_statistics.algorithmExecutions;

    double steeringSetpoint = 0.0;
    double motorSetpoint = 0.0;

    QString outputReadError;

    const bool steeringOutputValid =
        readOutputScalar(
            m_udpConfiguration.steeringOutputSignal,
            steeringSetpoint,
            outputReadError);

    if (!steeringOutputValid) {
        emit runtimeError(
            QStringLiteral(
                "Lenkwinkel-Soll konnte nicht gelesen werden: %1")
                .arg(outputReadError));
    }

    outputReadError.clear();

    const bool motorOutputValid =
        readOutputScalar(
            m_udpConfiguration.motorOutputSignal,
            motorSetpoint,
            outputReadError);

    if (!motorOutputValid) {
        emit runtimeError(
            QStringLiteral(
                "Motor-Soll konnte nicht gelesen werden: %1")
                .arg(outputReadError));
    }

    if (steeringOutputValid
        && motorOutputValid) {

        qDebug()
        << "Algorithmus-Ausgänge:"
        << "Lenkwinkel Soll ="
        << steeringSetpoint
        << "| Motor Soll ="
        << motorSetpoint;
    }

    /*
     * Nur senden, wenn der gemeinsame Steuerungssender
     * über die Checkbox aktiviert wurde.
     */
    if (m_udpConfiguration.command.enabled) {
        QString outputError;

        const QByteArray outputPacket =
            createOutputPacket(outputError);

        if (!outputError.isEmpty()) {
            emit runtimeError(outputError);
        }
        else if (!outputPacket.isEmpty()) {
            QString sendError;

            if (m_commandUdp.sendPacket(
                    outputPacket,
                    sendError)) {

                ++m_statistics.packetsSent;
            }
            else {
                emit runtimeError(sendError);
            }
        }
    }

    if (m_cycleTimer.isValid()) {
        const qint64 cycleEndNanoseconds =
            m_cycleTimer.nsecsElapsed();

        m_statistics.lastCycleTimeMs =
            static_cast<double>(
                cycleEndNanoseconds
                - cycleStartNanoseconds)
            / 1'000'000.0;
    }

    emit stepCompleted(m_currentStepCount);
}
