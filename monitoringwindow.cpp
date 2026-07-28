#include "monitoringwindow.h"
#include "ui_monitoringwindow.h"

#include <QVBoxLayout>
#include <QFrame>
#include <algorithm>
#include <cmath>

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
    const QVector<float> &x,
    const QVector<float> &y)
{
    if (!lidarPlot || lidarPlot->graphCount() == 0) {
        return;
    }

    const qsizetype pointCount =
        std::min(x.size(), y.size());

    QVector<double> plotX;
    QVector<double> plotY;

    plotX.reserve(pointCount);
    plotY.reserve(pointCount);

    for (qsizetype i = 0; i < pointCount; ++i) {
        const float xValue = x.at(i);
        const float yValue = y.at(i);

        /*
         * Ungültige LiDAR-Punkte nicht an QCustomPlot übergeben.
         */
        if (!std::isfinite(xValue)
            || !std::isfinite(yValue)) {
            continue;
        }

        plotX.append(static_cast<double>(xValue));
        plotY.append(static_cast<double>(yValue));
    }

    lidarPlot->graph(0)->setData(
        plotX,
        plotY);

    lidarPlot->replot(
        QCustomPlot::rpQueuedReplot);
}

void MonitoringWindow::setRuntimeValues(
    double steeringActual,
    double motorActual,
    double steeringSetpoint,
    double motorSetpoint)
{
    ui->labelSteeringActualValue->setText(
        QStringLiteral("%1 norm")
            .arg(steeringActual, 0, 'f', 3)
        );

    ui->labelMotorActualValue->setText(
        QStringLiteral("%1 RPM")
            .arg(motorActual, 0, 'f', 3)
        );

    ui->labelSteeringSetpointValue->setText(
        QStringLiteral("%1 norm")
            .arg(steeringSetpoint, 0, 'f', 3)
        );

    ui->labelMotorSetpointValue->setText(
        QStringLiteral("%1 norm")
            .arg(motorSetpoint, 0, 'f', 3)
        );
}

void MonitoringWindow::clearDiagnosticValues()
{
    for (QWidget *card : diagnosticCards)
    {
        ui->diagnosticsGrid->removeWidget(card);
        delete card;
    }

    diagnosticCards.clear();
}

void MonitoringWindow::setDiagnosticValues(
    const QVector<DiagnosticValue> &diagnostics)
{
    clearDiagnosticValues();

    constexpr int columnCount = 2;

    for (int index = 0; index < diagnostics.size(); ++index)
    {
        const DiagnosticValue &diagnostic = diagnostics.at(index);

        const int gridRow = index / columnCount;
        const int gridColumn = index % columnCount;

        auto *card = new QFrame(ui->diagnosticsContainer);
        card->setFrameShape(QFrame::StyledPanel);
        card->setFrameShadow(QFrame::Plain);
        card->setMinimumHeight(75);

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(10, 8, 10, 8);
        cardLayout->setSpacing(4);

        auto *nameLabel = new QLabel(diagnostic.name, card);
        nameLabel->setAlignment(Qt::AlignCenter);

        QString valueText;

        if (diagnostic.isBoolean) {
            valueText =
                diagnostic.value != 0.0
                    ? QStringLiteral("1")
                    : QStringLiteral("0");
        } else {
            valueText = QString::number(
                diagnostic.value,
                'f',
                3
                );
        }

        if (!diagnostic.unit.isEmpty())
        {
            valueText += QStringLiteral(" ");
            valueText += diagnostic.unit;
        }

        auto *valueLabel = new QLabel(valueText, card);
        valueLabel->setAlignment(Qt::AlignCenter);

        cardLayout->addWidget(nameLabel);
        cardLayout->addWidget(valueLabel);

        ui->diagnosticsGrid->addWidget(
            card,
            gridRow,
            gridColumn
            );

        diagnosticCards.append(card);
    }

    ui->diagnosticsGrid->setColumnStretch(0, 1);
    ui->diagnosticsGrid->setColumnStretch(1, 1);
}

MonitoringWindow::~MonitoringWindow()
{
    delete ui;
}