#include "algorithmruntime.h"

#include <QFile>

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

    const std::wstring nativePath =
        libraryPath.toStdWString();

    HMODULE module =
        LoadLibraryW(nativePath.c_str());

    if (!module) {
        return nullptr;
    }

    const QByteArray symbolBytes =
        symbolName.toLatin1();

    FARPROC symbol =
        GetProcAddress(
            module,
            symbolBytes.constData()
            );

    void *result =
        reinterpret_cast<void *>(symbol);

    /*
     * QLibrary hält die Bibliothek weiterhin geladen.
     * LoadLibraryW hat nur den Referenzzähler vorübergehend erhöht.
     */
    FreeLibrary(module);

    return result;

#else

    const QByteArray nativePath =
        QFile::encodeName(libraryPath);

    void *handle =
        dlopen(
            nativePath.constData(),
            RTLD_LAZY | RTLD_LOCAL
            );

    if (!handle) {
        return nullptr;
    }

    const QByteArray symbolBytes =
        symbolName.toLatin1();

    void *result =
        dlsym(
            handle,
            symbolBytes.constData()
            );

    /*
     * QLibrary hält die Bibliothek weiterhin geladen.
     */
    dlclose(handle);

    return result;

#endif
}

}

AlgorithmRuntime::AlgorithmRuntime(QObject *parent)
    : QObject(parent)
{
    /*
     * 20 ms entsprechen 50 Hz.
     */
    timer.setInterval(20);

    connect(
        &timer,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (!running || !stepFunction) {
                return;
            }

            if (inputContainer
                && lidarX.size() == 1601
                && lidarY.size() == 1601)
            {
                for (int index = 0; index < 1601; ++index)
                {
                    inputContainer->lidar_x[index] =
                        static_cast<float>(lidarX.at(index));

                    inputContainer->lidar_y[index] =
                        static_cast<float>(lidarY.at(index));
                }
            }

            if (inputContainer)
            {
                inputContainer->lenkwinkel_ist =
                    static_cast<float>(currentSteeringIst);

                inputContainer->motor_ist =
                    static_cast<float>(currentMotorIst);
            }

            stepFunction();

            ++currentStepCount;

            emit stepCompleted(currentStepCount);
        }
        );
}

void AlgorithmRuntime::setLidarPoints(
    const QVector<double> &x,
    const QVector<double> &y)
{
    lidarX = x;
    lidarY = y;
}

void AlgorithmRuntime::setVehicleState(
    double steeringIst,
    double motorIst)
{
    currentSteeringIst = steeringIst;
    currentMotorIst = motorIst;
}

double AlgorithmRuntime::steeringSoll() const
{
    if (!outputContainer) {
        return 0.0;
    }

    return outputContainer->lenkwinkel_soll;
}

double AlgorithmRuntime::motorSoll() const
{
    if (!outputContainer) {
        return 0.0;
    }

    return outputContainer->motor_soll;
}

RuntimeOutputs AlgorithmRuntime::outputs() const
{
    RuntimeOutputs result;

    if (!outputContainer) {
        return result;
    }

    result.steeringSoll =
        outputContainer->lenkwinkel_soll;

    result.phase =
        outputContainer->phase;

    result.motorSoll =
        outputContainer->motor_soll;

    result.speedMps =
        outputContainer->speed_mps;

    result.motorNorm =
        outputContainer->motor_norm;

    result.steeringNorm =
        outputContainer->steering_norm;

    return result;
}

AlgorithmRuntime::~AlgorithmRuntime()
{
    unload();
}


void AlgorithmRuntime::configure(
    const QString &newLibraryPath,
    const QString &newModelName)
{
    unload();

    libraryPath = newLibraryPath;
    modelName = newModelName;
}


bool AlgorithmRuntime::validate(QStringList &errors)
{
    errors.clear();

    clearResolvedSymbols();

    QString libraryError;

    if (!loadLibrary(libraryError)) {
        errors.append(
            QStringLiteral(
                "Algorithmusbibliothek konnte nicht geladen werden: %1"
                ).arg(libraryError)
            );

        return false;
    }

    const QString initializeName =
        modelName + QStringLiteral("_initialize");

    const QString stepName =
        modelName + QStringLiteral("_step");

    const QString terminateName =
        modelName + QStringLiteral("_terminate");

    initializeFunction =
        reinterpret_cast<InitializeFunction>(
            library.resolve(
                initializeName.toLatin1().constData()
                )
            );

    if (!initializeFunction) {
        errors.append(
            QStringLiteral(
                "Funktion %1 wurde nicht gefunden."
                ).arg(initializeName)
            );
    }

    stepFunction =
        reinterpret_cast<StepFunction>(
            library.resolve(
                stepName.toLatin1().constData()
                )
            );

    if (!stepFunction) {
        errors.append(
            QStringLiteral(
                "Funktion %1 wurde nicht gefunden."
                ).arg(stepName)
            );
    }

    terminateFunction =
        reinterpret_cast<TerminateFunction>(
            library.resolve(
                terminateName.toLatin1().constData()
                )
            );

    const QString inputName =
        modelName + QStringLiteral("_U");

    inputContainer =
        reinterpret_cast<WandfolgenInputs *>(
            resolveDataSymbol(
                libraryPath,
                inputName
                )
            );

    if (!inputContainer) {
        errors.append(
            QStringLiteral(
                "Container %1 wurde nicht gefunden."
                ).arg(inputName)
            );
    }

    const QString outputName =
        modelName + QStringLiteral("_Y");

    outputContainer =
        reinterpret_cast<WandfolgenOutputs *>(
            resolveDataSymbol(
                libraryPath,
                outputName
                )
            );

    if (!outputContainer) {
        errors.append(
            QStringLiteral(
                "Container %1 wurde nicht gefunden."
                ).arg(outputName)
            );
    }

    if (!terminateFunction) {
        errors.append(
            QStringLiteral(
                "Funktion %1 wurde nicht gefunden."
                ).arg(terminateName)
            );
    }

    if (!errors.isEmpty()) {
        unload();
        return false;
    }

    return true;
}


bool AlgorithmRuntime::start(QString &errorMessage)
{
    errorMessage.clear();

    if (running) {
        errorMessage =
            QStringLiteral(
                "Der Algorithmus läuft bereits."
                );

        return false;
    }

    if (!library.isLoaded()
        || !initializeFunction
        || !stepFunction
        || !terminateFunction) {
        errorMessage =
            QStringLiteral(
                "Die Algorithmusbibliothek wurde nicht "
                "vollständig validiert."
                );

        return false;
    }

    currentStepCount = 0;

    if (!inputContainer) {
        errorMessage =
            QStringLiteral(
                "Der Eingangscontainer ist nicht verfügbar."
                );

        return false;
    }

    initializeFunction();

    running = true;
    timer.start();

    return true;
}


void AlgorithmRuntime::stop()
{
    timer.stop();

    if (running && terminateFunction) {
        terminateFunction();
    }

    running = false;
}


void AlgorithmRuntime::unload()
{
    stop();

    clearResolvedSymbols();

    if (library.isLoaded()) {
        library.unload();
    }

    currentStepCount = 0;
}


bool AlgorithmRuntime::isLoaded() const
{
    return library.isLoaded();
}


bool AlgorithmRuntime::isRunning() const
{
    return running;
}


quint64 AlgorithmRuntime::stepCount() const
{
    return currentStepCount;
}


bool AlgorithmRuntime::loadLibrary(QString &errorMessage)
{
    errorMessage.clear();

    if (libraryPath.isEmpty()) {
        errorMessage =
            QStringLiteral(
                "Es wurde kein Bibliothekspfad konfiguriert."
                );

        return false;
    }

    if (modelName.isEmpty()) {
        errorMessage =
            QStringLiteral(
                "Es wurde kein Modellname konfiguriert."
                );

        return false;
    }

    if (library.isLoaded()) {
        library.unload();
    }

    library.setFileName(libraryPath);

    if (!library.load()) {
        errorMessage = library.errorString();
        return false;
    }

    return true;
}


void AlgorithmRuntime::clearResolvedSymbols()
{
    initializeFunction = nullptr;
    stepFunction = nullptr;
    terminateFunction = nullptr;
    inputContainer = nullptr;
    outputContainer = nullptr;
}