#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QJsonArray>
#include <QLibrary>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QTimer>

class QComboBox;
class QLineEdit;
class QTableWidget;
class QWidget;
class MonitoringWindow;

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

        SignalRole role = SignalRole::Unknown;
    };

    struct MonitoringRow
    {
        QWidget *rowWidget = nullptr;
        QLineEdit *editName = nullptr;
        QComboBox *comboSignal = nullptr;
        QLineEdit *editUnit = nullptr;
    };

    using InitializeFunction = void (*)();
    using StepFunction = void (*)();
    using TerminateFunction = void (*)();

    void selectAlgorithmPackage();
    void invalidateValidation();

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

    bool hasDimensions(
        const SignalInfo &signal,
        int rows,
        int columns
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
        int requiredRows,
        int requiredColumns
        );

    QList<SignalInfo> monitoringSignals(
        const QList<SignalInfo> &signalList
        ) const;

    bool loadAlgorithmLibrary(
        QString &errorMessage
        );

    bool validateAlgorithmLibrary(
        QStringList &errors
        );

    Ui::MainWindow *ui;

    MonitoringWindow *monitoringWindow = nullptr;

    QString algorithmPackagePath;
    QString algorithmLibraryPath;
    QString algorithmModelName;

    QLibrary algorithmLibrary;

    QList<SignalInfo> inputSignals;
    QList<SignalInfo> outputSignals;
    QList<MonitoringRow> monitoringRows;

    InitializeFunction initializeFunction = nullptr;
    StepFunction stepFunction = nullptr;
    TerminateFunction terminateFunction = nullptr;

    QTimer algorithmTimer;
    quint64 algorithmStepCount = 0;
    bool algorithmRunning = false;
};

#endif // MAINWINDOW_H