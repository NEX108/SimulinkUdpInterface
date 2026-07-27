#ifndef MONITORINGWINDOW_H
#define MONITORINGWINDOW_H

#include <QDialog>
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
    double value;
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
    void constrainLidarPlotRange();
    void clearDiagnosticValues();

    Ui::MonitoringWindow *ui;
    QCustomPlot *lidarPlot = nullptr;

    QVector<QWidget *> diagnosticCards;
};

#endif // MONITORINGWINDOW_H