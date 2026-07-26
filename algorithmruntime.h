#ifndef ALGORITHMRUNTIME_H
#define ALGORITHMRUNTIME_H

#include <QLibrary>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

struct WandfolgenInputs
{
    float lidar_x[1601];
    float lidar_y[1601];
    float lenkwinkel_ist;
    float motor_ist;
};

struct WandfolgenOutputs
{
    double lenkwinkel_soll;
    double phase;
    double motor_soll;
    double speed_mps;
    float motor_norm;
    float steering_norm;
};

struct RuntimeOutputs
{
    double steeringSoll = 0.0;
    double phase = 0.0;
    double motorSoll = 0.0;
    double speedMps = 0.0;
    float motorNorm = 0.0f;
    float steeringNorm = 0.0f;
};

class AlgorithmRuntime : public QObject
{
    Q_OBJECT

public:
    explicit AlgorithmRuntime(QObject *parent = nullptr);
    ~AlgorithmRuntime() override;

    void configure(
        const QString &libraryPath,
        const QString &modelName
        );

    bool validate(QStringList &errors);

    bool start(QString &errorMessage);
    void stop();
    void unload();

    void setLidarPoints(
        const QVector<double> &x,
        const QVector<double> &y);

    void setVehicleState(
        double steeringIst,
        double motorIst);

    double steeringSoll() const;
    double motorSoll() const;

    RuntimeOutputs outputs() const;

    bool isLoaded() const;
    bool isRunning() const;

    quint64 stepCount() const;

signals:
    void stepCompleted(quint64 stepCount);
    void runtimeError(const QString &message);

private:
    using InitializeFunction = void (*)();
    using StepFunction = void (*)();
    using TerminateFunction = void (*)();
//    using ContainerPointer = void *;

    bool loadLibrary(QString &errorMessage);
    void clearResolvedSymbols();

    QString libraryPath;
    QString modelName;

    QLibrary library;
    QTimer timer;

    QVector<double> lidarX;
    QVector<double> lidarY;

    double currentSteeringIst = 0.0;
    double currentMotorIst = 0.0;

    InitializeFunction initializeFunction = nullptr;
    StepFunction stepFunction = nullptr;
    TerminateFunction terminateFunction = nullptr;
//    ContainerPointer inputContainer = nullptr;
//    ContainerPointer outputContainer = nullptr;
    WandfolgenInputs *inputContainer = nullptr;
    WandfolgenOutputs *outputContainer = nullptr;
    quint64 currentStepCount = 0;
    bool running = false;
};

#endif // ALGORITHMRUNTIME_H