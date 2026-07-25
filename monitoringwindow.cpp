#include "monitoringwindow.h"
#include "ui_monitoringwindow.h"
#include <QVBoxLayout>

MonitoringWindow::MonitoringWindow(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MonitoringWindow)
{
    ui->setupUi(this);

    auto *plotLayout = new QVBoxLayout(ui->lidarPlotPlaceholder);
    plotLayout->setContentsMargins(0, 0, 0, 0);

    lidarPlot = new QCustomPlot(ui->lidarPlotPlaceholder);
    lidarPlot->setAntialiasedElements(QCP::aeAll);
    plotLayout->addWidget(lidarPlot);

    lidarPlot->xAxis->setRange(-12.0, 12.0);
    lidarPlot->yAxis->setRange(-12.0, 12.0);

    lidarPlot->xAxis->setLabel("X [m]");
    lidarPlot->yAxis->setLabel("Y [m]");

    lidarPlot->xAxis->setUpperEnding(QCPLineEnding::esNone);
    lidarPlot->yAxis->setUpperEnding(QCPLineEnding::esNone);

    lidarPlot->addGraph();
    lidarPlot->graph(0)->setLineStyle(QCPGraph::lsNone);
    lidarPlot->graph(0)->setScatterStyle(
        QCPScatterStyle(
            QCPScatterStyle::ssCircle, // Kreis
            QPen(Qt::blue), // blauer Rand
            QBrush(Qt::blue), // Blaue Füllung
            2 // Durchmesser in Pixel
            )
        );

    lidarPlot->setInteractions(
        QCP::iRangeDrag |
        QCP::iRangeZoom
        );

    lidarPlot->axisRect()->setRangeDrag(
        Qt::Horizontal | Qt::Vertical
        );

    lidarPlot->axisRect()->setRangeZoom(
        Qt::Horizontal | Qt::Vertical
        );

    lidarPlot->xAxis->grid()->setVisible(true);
    lidarPlot->yAxis->grid()->setVisible(true);

    QPen gridPen(QColor(220,220,220));
    gridPen.setStyle(Qt::DashLine);

    lidarPlot->xAxis->grid()->setPen(gridPen);
    lidarPlot->yAxis->grid()->setPen(gridPen);

    lidarPlot->setBackground(Qt::white);

    lidarPlot->legend->setVisible(false);

    QPen originPen(QColor(120, 120, 120));
    originPen.setWidth(1);

    auto *horizontalOrigin = new QCPItemStraightLine(lidarPlot);
    horizontalOrigin->setPen(originPen);
    horizontalOrigin->point1->setCoords(0.0, 0.0);
    horizontalOrigin->point2->setCoords(1.0, 0.0);

    auto *verticalOrigin = new QCPItemStraightLine(lidarPlot);
    verticalOrigin->setPen(originPen);
    verticalOrigin->point1->setCoords(0.0, 0.0);
    verticalOrigin->point2->setCoords(0.0, 1.0);

    connect(
        lidarPlot->xAxis,
        QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
        this,
        [this](const QCPRange &)
        {
            constrainLidarPlotRange();
        }
        );

    connect(
        lidarPlot->yAxis,
        QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
        this,
        [this](const QCPRange &)
        {
            constrainLidarPlotRange();
        }
        );
}

void MonitoringWindow::constrainLidarPlotRange()
{
    constexpr double maxRange = 13.0;
    constexpr double minSpan = 0.5;

    static bool correctingRange = false;

    if (correctingRange) {
        return;
    }

    correctingRange = true;

    QCPRange xRange = lidarPlot->xAxis->range();
    QCPRange yRange = lidarPlot->yAxis->range();

    double xSpan = xRange.size();
    double ySpan = yRange.size();

    xSpan = qBound(minSpan, xSpan, 2.0 * maxRange);
    ySpan = qBound(minSpan, ySpan, 2.0 * maxRange);

    double xCenter = xRange.center();
    double yCenter = yRange.center();

    const double maxXCenter = maxRange - xSpan / 2.0;
    const double maxYCenter = maxRange - ySpan / 2.0;

    xCenter = qBound(-maxXCenter, xCenter, maxXCenter);
    yCenter = qBound(-maxYCenter, yCenter, maxYCenter);

    lidarPlot->xAxis->setRange(
        xCenter - xSpan / 2.0,
        xCenter + xSpan / 2.0
        );

    lidarPlot->yAxis->setRange(
        yCenter - ySpan / 2.0,
        yCenter + ySpan / 2.0
        );

    correctingRange = false;
}

void MonitoringWindow::setLidarPoints(
    const QVector<double> &x,
    const QVector<double> &y)
{
    if (!lidarPlot->graphCount())
        return;

    lidarPlot->graph(0)->setData(x, y);

    lidarPlot->replot();
}

MonitoringWindow::~MonitoringWindow()
{
    delete ui;
}