#ifndef MONITORINGWINDOW_H
#define MONITORINGWINDOW_H

#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QVector>
#include <QString>

#include "qcustomplot.h"

namespace Ui {
class MonitoringWindow;
}

struct DiagnosticValue
{
    QString name;
    QString unit;
    double value = 0.0;
    bool isBoolean = false;
};

class MonitoringWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MonitoringWindow(QWidget *parent = nullptr);
    ~MonitoringWindow();

    void setLidarPoints(
        const QVector<float> &x,
        const QVector<float> &y);

    void setRuntimeValues(
        double steeringActual,
        double motorActual,
        double steeringSetpoint,
        double motorSetpoint);

    void setDiagnosticValues(
        const QVector<DiagnosticValue> &diagnostics);

private:
    void scheduleLidarPlotRangeCorrection();
    void constrainLidarPlotRange();
    void clearDiagnosticValues();

    Ui::MonitoringWindow *ui;
    QCustomPlot *lidarPlot = nullptr;

    bool rangeCorrectionPending = false;
    bool correctingRange = false;

    QElapsedTimer lidarUpdateTimer;

    QVector<QWidget *> diagnosticCards;
};

#endif // MONITORINGWINDOW_H