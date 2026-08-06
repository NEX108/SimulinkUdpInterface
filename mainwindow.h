#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QJsonArray>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>

class QComboBox;
class QLineEdit;
class QTableWidget;
class QWidget;
class MonitoringWindow;
class AlgorithmRuntime;
class QCheckBox;

#include "validationdialog.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    enum class SignalRole
    {
        Unknown,
        Lidar,
        Scalar,
        Monitoring
    };

    struct SignalInfo
    {
        QString name;
        QString dataType;
        QString cType;
        QList<int> dimensions;
        qsizetype elementCount = 0;

        SignalRole role = SignalRole::Unknown;
    };

    struct MonitoringRow
    {
        QWidget *rowWidget = nullptr;
        QLineEdit *editName = nullptr;
        QComboBox *comboSignal = nullptr;
        QLineEdit *editUnit = nullptr;
    };

    void appendLog(
        const QString &title,
        const QStringList &details = {}
        );

    void logValidationResult(
        const ValidationData &data,
        bool accepted
        );

    void selectAlgorithmPackage();
    void invalidateValidation();
    void initializeOverviewPage();

    void addMonitoringSignalRow();
    void clearMonitoringRows();

    QList<SignalInfo> readSignals(
        const QJsonArray &array,
        bool isInput
        ) const;

    SignalRole determineSignalRole(
        const SignalInfo &signal,
        bool isInput
        ) const;

    bool isNumericSignal(
        const SignalInfo &signal
        ) const;

    bool isBooleanSignal(
        const SignalInfo &signal
        ) const;

    QString dimensionText(
        const QList<int> &dimensions
        ) const;

    void fillSignalTable(
        QTableWidget *table,
        const QList<SignalInfo> &signalList
        );

    void fillSignalComboBox(
        QComboBox *comboBox,
        const QList<SignalInfo> &signalList,
        SignalRole requiredRole
        );

    void fillCommandEnableComboBox(
        QComboBox *comboBox,
        const QList<SignalInfo> &signalList
        );

    QList<SignalInfo> monitoringSignals(
        const QList<SignalInfo> &signalList
        ) const;

    bool shouldWriteDynamicLog(
        const QString &category
        );

    void initializeRecordingPage();

    void updateRecordingPageState();

    void updateRecordedDiagnostics();

    void updateRecordingFilePreviews();

    void resetRecordingFilename();

    void selectRecordingPath();

    void updateRecordSignalCheckBox(
        QCheckBox *checkBox,
        const QString &signalKey,
        bool signalAvailable
        );

    Ui::MainWindow *ui;

    MonitoringWindow *monitoringWindow = nullptr;
    AlgorithmRuntime *algorithmRuntime = nullptr;

    QElapsedTimer logTimer;
    QElapsedTimer liveMonitorUpdateTimer;

    QHash<QString, qint64> lastLogTimes;

    double steeringActual = 0.0;
    double motorActual = 0.0;

    QString algorithmFolder;

    QList<SignalInfo> inputSignals;
    QList<SignalInfo> outputSignals;
    QList<MonitoringRow> monitoringRows;

    QHash<QString, bool> recordingSignalAvailability;

    QHash<QString, QCheckBox *>
        recordedDiagnosticCheckBoxes;

    QHash<QString, bool>
        recordedDiagnosticSelections;
};

#endif // MAINWINDOW_H