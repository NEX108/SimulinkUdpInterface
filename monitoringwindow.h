#ifndef MONITORINGWINDOW_H
#define MONITORINGWINDOW_H

#include <QDialog>
#include "qcustomplot.h"
#include "qcustomplot.h"

namespace Ui {
class MonitoringWindow;
}

class MonitoringWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MonitoringWindow(QWidget *parent = nullptr);
    ~MonitoringWindow();

    void setLidarPoints(
        const QVector<double>& x,
        const QVector<double>& y);

private:
    Ui::MonitoringWindow *ui;

    QCustomPlot *lidarPlot = nullptr;

    void constrainLidarPlotRange();
};

#endif // MONITORINGWINDOW_H