#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "validationdialog.h"
#include "monitoringwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    /*
     * Laufzeittimer
     * 20 ms entsprechen 50 Hz.
     */
    algorithmTimer.setInterval(20);

    connect(
        &algorithmTimer,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (!stepFunction) {
                return;
            }

            stepFunction();
            ++algorithmStepCount;

            // Statusanzeige nur ungefähr einmal pro Sekunde aktualisieren.
            if (algorithmStepCount % 50 == 0) {
                ui->statusBar->showMessage(
                    QStringLiteral(
                        "Algorithmus läuft – Schritte: %1"
                    ).arg(algorithmStepCount)
                );
            }
        }
    );

    /*
     * Algorithmuspaket laden
     */
    connect(
        ui->buttonLoadAlgorithm,
        &QPushButton::clicked,
        this,
        &MainWindow::selectAlgorithmPackage
    );

    /*
     * Konfiguration validieren
     */
    connect(
        ui->buttonValidate,
        &QPushButton::clicked,
        this,
        [this]()
        {
            ValidationData data;

            /*
             * Aktuelle Signalzuordnungen
             */
            const bool lidarXAssigned =
                ui->comboLidarX->currentIndex() > 0;

            const bool lidarYAssigned =
                ui->comboLidarY->currentIndex() > 0;

            const bool steeringInAssigned =
                ui->comboSteeringIn->currentIndex() > 0;

            const bool motorInAssigned =
                ui->comboMotorIn->currentIndex() > 0;

            const bool steeringOutAssigned =
                ui->comboSteeringOut->currentIndex() > 0;

            const bool motorOutAssigned =
                ui->comboMotorOut->currentIndex() > 0;

            const QString steeringOutSignal =
                steeringOutAssigned
                    ? ui->comboSteeringOut->currentText()
                    : QString();

            const QString motorOutSignal =
                motorOutAssigned
                    ? ui->comboMotorOut->currentText()
                    : QString();

            /*
             * Bibliothek prüfen
             */
            QStringList libraryErrors;

            validateAlgorithmLibrary(libraryErrors);

            for (const QString &error : libraryErrors) {
                data.errors.append(error);
            }

            /*
             * Diagnosesignale
             */
            QStringList usedMonitoringSignals;

            for (const MonitoringRow &row : monitoringRows) {
                const QString displayName =
                    row.editName->text().trimmed();

                const QString signalName =
                    row.comboSignal->currentIndex() > 0
                        ? row.comboSignal->currentText()
                        : QString();

                const QString unit =
                    row.editUnit->text().trimmed();

                // Vollständig leere Zeilen werden ignoriert.
                // Vollständig leere Zeilen werden ignoriert.
                if (displayName.isEmpty()
                    && signalName.isEmpty()
                    && unit.isEmpty()) {
                    continue;
                }

                if (signalName.isEmpty()) {
                    data.errors.append(
                        QStringLiteral(
                            "Ein Diagnosesignal besitzt keine "
                            "Signalzuordnung."
                            )
                        );
                    continue;
                }

                if (usedMonitoringSignals.contains(signalName)) {
                    data.errors.append(
                        QStringLiteral(
                            "Das Diagnosesignal \"%1\" wurde "
                            "mehrfach zugeordnet."
                            ).arg(signalName)
                        );
                    continue;
                }

                if (signalName == steeringOutSignal
                    || signalName == motorOutSignal) {
                    data.errors.append(
                        QStringLiteral(
                            "Das Signal \"%1\" ist bereits als "
                            "Steuerungsausgang zugeordnet und kann "
                            "nicht zusätzlich als Diagnosesignal "
                            "verwendet werden."
                            ).arg(signalName)
                        );
                    continue;
                }

                usedMonitoringSignals.append(signalName);

                const QString effectiveDisplayName =
                    displayName.isEmpty()
                        ? signalName
                        : displayName;

                QString entry =
                    QStringLiteral("%1 → %2")
                        .arg(effectiveDisplayName, signalName);

                if (!unit.isEmpty()) {
                    entry +=
                        QStringLiteral(" [%1]").arg(unit);
                }

                data.monitoring.append(entry);
            }

            if (data.monitoring.isEmpty()) {
                data.warnings.append(
                    QStringLiteral(
                        "Es wurden keine Diagnosesignale konfiguriert."
                        )
                    );
            }

            /*
             * LiDAR-Eingang
             */
            const bool lidarEnabled =
                ui->checkLidarReceiveEnabled->isChecked();

            if (!lidarEnabled) {
                data.inputs.append(
                    QStringLiteral("LiDAR X → Nicht zugeteilt")
                    );

                data.inputs.append(
                    QStringLiteral("LiDAR Y → Nicht zugeteilt")
                    );

                data.warnings.append(
                    QStringLiteral(
                        "LiDAR-Empfang ist deaktiviert."
                        )
                    );
            }
            else if (!lidarXAssigned || !lidarYAssigned) {
                data.inputs.append(
                    QStringLiteral("LiDAR X → %1")
                        .arg(
                            lidarXAssigned
                                ? ui->comboLidarX->currentText()
                                : QStringLiteral("Nicht zugeteilt")
                            )
                    );

                data.inputs.append(
                    QStringLiteral("LiDAR Y → %1")
                        .arg(
                            lidarYAssigned
                                ? ui->comboLidarY->currentText()
                                : QStringLiteral("Nicht zugeteilt")
                            )
                    );

                data.errors.append(
                    QStringLiteral(
                        "LiDAR-Empfang ist aktiviert, aber "
                        "LiDAR X oder LiDAR Y wurde nicht zugeordnet."
                        )
                    );
            }
            else {
                data.inputs.append(
                    QStringLiteral(
                        "LiDAR X → %1 → Port: %2"
                        )
                        .arg(ui->comboLidarX->currentText())
                        .arg(ui->spinLidarReceivePort->value())
                    );

                data.inputs.append(
                    QStringLiteral(
                        "LiDAR Y → %1 → Port: %2"
                        )
                        .arg(ui->comboLidarY->currentText())
                        .arg(ui->spinLidarReceivePort->value())
                    );

                if (ui->comboLidarX->currentText()
                    == ui->comboLidarY->currentText()) {
                    data.errors.append(
                        QStringLiteral(
                            "LiDAR X und LiDAR Y dürfen nicht "
                            "demselben Algorithmussignal zugeordnet werden."
                            )
                        );
                }
            }

            /*
             * Lenkwinkel-Ist-Eingang
             */
            const bool steeringReceiveEnabled =
                ui->checkSteeringReceiveEnabled->isChecked();

            if (!steeringReceiveEnabled) {
                data.inputs.append(
                    QStringLiteral(
                        "Lenkwinkel Ist → Nicht zugeteilt"
                        )
                    );

                data.warnings.append(
                    QStringLiteral(
                        "Lenkwinkel-Ist-Empfang ist deaktiviert."
                        )
                    );
            }
            else if (!steeringInAssigned) {
                data.inputs.append(
                    QStringLiteral(
                        "Lenkwinkel Ist → Nicht zugeteilt"
                        )
                    );

                data.errors.append(
                    QStringLiteral(
                        "Lenkwinkel-Ist-Empfang ist aktiviert, "
                        "aber kein Algorithmuseingang wurde zugeordnet."
                        )
                    );
            }
            else {
                data.inputs.append(
                    QStringLiteral(
                        "Lenkwinkel Ist → %1 → Port: %2"
                        )
                        .arg(ui->comboSteeringIn->currentText())
                        .arg(ui->spinSteeringReceivePort->value())
                    );
            }

            /*
             * Motor-Ist-Eingang
             */
            const bool motorReceiveEnabled =
                ui->checkMotorReceiveEnabled->isChecked();

            if (!motorReceiveEnabled) {
                data.inputs.append(
                    QStringLiteral(
                        "Motor Ist → Nicht zugeteilt"
                        )
                    );

                data.warnings.append(
                    QStringLiteral(
                        "Motor-Ist-Empfang ist deaktiviert."
                        )
                    );
            }
            else if (!motorInAssigned) {
                data.inputs.append(
                    QStringLiteral(
                        "Motor Ist → Nicht zugeteilt"
                        )
                    );

                data.errors.append(
                    QStringLiteral(
                        "Motor-Ist-Empfang ist aktiviert, "
                        "aber kein Algorithmuseingang wurde zugeordnet."
                        )
                    );
            }
            else {
                data.inputs.append(
                    QStringLiteral(
                        "Motor Ist → %1 → Port: %2"
                        )
                        .arg(ui->comboMotorIn->currentText())
                        .arg(ui->spinMotorReceivePort->value())
                    );
            }

            if (steeringReceiveEnabled
                && motorReceiveEnabled
                && steeringInAssigned
                && motorInAssigned
                && ui->comboSteeringIn->currentText()
                       == ui->comboMotorIn->currentText()) {
                data.errors.append(
                    QStringLiteral(
                        "Lenkwinkel Ist und Motor Ist dürfen nicht "
                        "demselben Algorithmuseingang zugeordnet werden."
                        )
                    );
            }

            /*
             * Gemeinsame Steuerungsausgabe
             *
             * Lenkwinkel Soll und Motor Soll werden als getrennte
             * Algorithmusausgänge ausgewählt, aber gemeinsam über
             * einen UDP-Port gesendet.
             */
            const bool commandSendEnabled =
                ui->checkCommandSendEnabled->isChecked();

            if (!commandSendEnabled) {
                data.outputs.append(
                    QStringLiteral(
                        "Lenkwinkel Soll → Nicht zugeteilt"
                        )
                    );

                data.outputs.append(
                    QStringLiteral(
                        "Motor Soll → Nicht zugeteilt"
                        )
                    );

                data.warnings.append(
                    QStringLiteral(
                        "Steuerungsausgabe ist deaktiviert."
                        )
                    );
            }
            else {
                data.outputs.append(
                    QStringLiteral("Lenkwinkel Soll → %1")
                        .arg(
                            steeringOutAssigned
                                ? steeringOutSignal
                                : QStringLiteral("Nicht zugeteilt")
                            )
                    );

                data.outputs.append(
                    QStringLiteral("Motor Soll → %1")
                        .arg(
                            motorOutAssigned
                                ? motorOutSignal
                                : QStringLiteral("Nicht zugeteilt")
                            )
                    );

                if (!steeringOutAssigned || !motorOutAssigned) {
                    data.errors.append(
                        QStringLiteral(
                            "Die Steuerungsausgabe ist aktiviert, "
                            "aber Lenkwinkel Soll oder Motor Soll "
                            "wurde nicht zugeordnet."
                            )
                        );
                }

                if (steeringOutAssigned
                    && motorOutAssigned
                    && steeringOutSignal == motorOutSignal) {
                    data.errors.append(
                        QStringLiteral(
                            "Lenkwinkel Soll und Motor Soll dürfen "
                            "nicht demselben Algorithmusausgang "
                            "zugeordnet werden."
                            )
                        );
                }

                const QString targetIp =
                    ui->editCommandTargetIp
                        ->text()
                        .trimmed();

                if (targetIp.isEmpty()) {
                    data.errors.append(
                        QStringLiteral(
                            "Die Steuerungsausgabe ist aktiviert, "
                            "aber die Ziel-IP fehlt."
                            )
                        );
                }

                if (steeringOutAssigned
                    && motorOutAssigned
                    && steeringOutSignal != motorOutSignal
                    && !targetIp.isEmpty()) {
                    data.outputs.removeLast();
                    data.outputs.removeLast();

                    data.outputs.append(
                        QStringLiteral(
                            "Lenkwinkel Soll → %1 → IP:Port: %2:%3"
                            )
                            .arg(steeringOutSignal)
                            .arg(targetIp)
                            .arg(ui->spinCommandTargetPort->value())
                        );

                    data.outputs.append(
                        QStringLiteral(
                            "Motor Soll → %1 → IP:Port: %2:%3"
                            )
                            .arg(motorOutSignal)
                            .arg(targetIp)
                            .arg(ui->spinCommandTargetPort->value())
                        );
                }
            }

            /*
             * Parameter
             *
             * Werden später ergänzt.
             */

            /*
             * Validierungsdialog
             */
            ValidationDialog dialog(this);
            dialog.setValidationData(data);

            const int result = dialog.exec();

            const bool validationAccepted =
                result == QDialog::Accepted
                && data.errors.isEmpty();

            ui->buttonStart->setEnabled(validationAccepted);

            if (!validationAccepted) {
                initializeFunction = nullptr;
                stepFunction = nullptr;
                terminateFunction = nullptr;

                if (algorithmLibrary.isLoaded()) {
                    algorithmLibrary.unload();
                }
            }
        }
        );

    /*
     * Algorithmus starten
     */
    connect(
        ui->buttonStart,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if (!algorithmLibrary.isLoaded()
                || !initializeFunction
                || !stepFunction
                || !terminateFunction) {
                QMessageBox::critical(
                    this,
                    QStringLiteral("Start fehlgeschlagen"),
                    QStringLiteral(
                        "Die Algorithmusbibliothek ist nicht "
                        "vollständig geladen.\n"
                        "Bitte erneut validieren."
                        )
                    );
                return;
            }

            algorithmStepCount = 0;

            initializeFunction();

            algorithmRunning = true;
            algorithmTimer.start();

            ui->statusBar->showMessage(
                QStringLiteral(
                    "Algorithmus erfolgreich gestartet."
                    )
                );

            ui->buttonStart->setEnabled(false);
            ui->buttonStop->setEnabled(true);
        }
        );

    /*
     * Algorithmus stoppen
     */
    connect(
        ui->buttonStop,
        &QPushButton::clicked,
        this,
        [this]()
        {
            algorithmTimer.stop();

            if (algorithmRunning && terminateFunction) {
                terminateFunction();
            }

            algorithmRunning = false;

            initializeFunction = nullptr;
            stepFunction = nullptr;
            terminateFunction = nullptr;

            if (algorithmLibrary.isLoaded()) {
                algorithmLibrary.unload();
            }

            ui->buttonStart->setEnabled(false);
            ui->buttonStop->setEnabled(false);

            ui->statusBar->showMessage(
                QStringLiteral(
                    "Algorithmus gestoppt – insgesamt %1 Schritte."
                    ).arg(algorithmStepCount)
                );
        }
        );

    /*
     * Monitoring-Zeile hinzufügen
     */
    connect(
        ui->buttonAddMonitoringSignal,
        &QPushButton::clicked,
        this,
        &MainWindow::addMonitoringSignalRow
        );

    /*
     * Navigation
     */
    const QList<QWidget *> pages{
        ui->overviewPage,
        ui->inputsPage,
        ui->outputsPage,
        ui->monitoringPage,
        ui->debugPage,
        ui->parametersPage,
        ui->liveMonitorPage,
        ui->logsPage
    };

    connect(
        ui->navigationList,
        &QListWidget::itemClicked,
        this,
        [this](QListWidgetItem *item)
        {
            if (!item) {
                return;
            }

            const int row = ui->navigationList->row(item);

            constexpr int liveMonitorRow = 6; //Falls sich die Navigationszeile von LiveMonitoring ändert hier auch ändern!

            if (row != liveMonitorRow) {
                return;
            }

            if (!monitoringWindow) {
                monitoringWindow = new MonitoringWindow(this);
            }

            monitoringWindow->show();
            monitoringWindow->raise();
            monitoringWindow->activateWindow();

            QVector<double> lidarX;
            QVector<double> lidarY;

            //Senkrechte Wand bei x=8
            for (double y = -6.0; y <= 0.5; y += 0.1)
            {
                lidarX.append(8.0);
                lidarY.append(y);
            }

            // Horizontale Wand bei y = -6 m
            for (double x = -8.0; x <= 8.0; x += 0.1)
            {
                lidarX.append(x);
                lidarY.append(-6.0);
            }

            // Horizontale Wand bei y = 0.5 m
            for (double x = -8.0; x <= 8.0; x += 0.1)
            {
                lidarX.append(x);
                lidarY.append(0.5);
            }

            monitoringWindow->setLidarPoints(lidarX, lidarY);

            ui->statusBar->showMessage(
                QStringLiteral("Live-Monitor geöffnet.")
                );
        }
        );

    connect(
        ui->navigationList,
        &QListWidget::currentRowChanged,
        this,
        [this, pages](int row)
        {
            if (row < 0 || row >= pages.size()) {
                return;
            }

            constexpr int liveMonitorRow = 6; //Falls sich die Navigationszeile von LiveMonitoring ändert hier auch ändern!

            if (row == liveMonitorRow) {
                return;
            }

            ui->contentStack->setCurrentWidget(
                pages.at(row)
                );

            if (const QListWidgetItem *item =
                ui->navigationList->item(row)) {

                ui->statusBar->showMessage(
                    QStringLiteral("Seite: %1")
                        .arg(item->text())
                    );
            }
        }
        );

    /*
     * Änderungen an Signalzuordnungen
     */
    connect(
        ui->comboLidarX,
        &QComboBox::currentIndexChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->comboLidarY,
        &QComboBox::currentIndexChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->comboSteeringIn,
        &QComboBox::currentIndexChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->comboMotorIn,
        &QComboBox::currentIndexChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->comboSteeringOut,
        &QComboBox::currentIndexChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->comboMotorOut,
        &QComboBox::currentIndexChanged,
        this,
        &MainWindow::invalidateValidation
        );

    /*
     * Änderungen an UDP-Ports
     */
    connect(
        ui->spinLidarReceivePort,
        &QSpinBox::valueChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->spinSteeringReceivePort,
        &QSpinBox::valueChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->spinMotorReceivePort,
        &QSpinBox::valueChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->spinCommandTargetPort,
        &QSpinBox::valueChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->editCommandTargetIp,
        &QLineEdit::textChanged,
        this,
        &MainWindow::invalidateValidation
        );

    /*
     * Änderungen an Aktivierungszuständen
     */
    connect(
        ui->checkLidarReceiveEnabled,
        &QCheckBox::toggled,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->checkSteeringReceiveEnabled,
        &QCheckBox::toggled,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->checkMotorReceiveEnabled,
        &QCheckBox::toggled,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        ui->checkCommandSendEnabled,
        &QCheckBox::toggled,
        this,
        &MainWindow::invalidateValidation
        );

    /*
     * Startansicht
     */
    ui->navigationList->setCurrentRow(0);
    ui->contentStack->setCurrentWidget(
        ui->overviewPage
        );
}


QList<MainWindow::SignalInfo> MainWindow::readSignals(
    const QJsonArray &array,
    bool isInput) const
{
    QList<SignalInfo> signalList;

    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();

        SignalInfo signal;

        signal.name =
            object.value(
                      QStringLiteral("name")
                      ).toString();

        signal.dataType =
            object.value(
                      QStringLiteral("dataType")
                      ).toString();

        signal.cType =
            object.value(
                      QStringLiteral("cType")
                      ).toString();

        const QJsonArray dimensions =
            object.value(
                      QStringLiteral("dimensions")
                      ).toArray();

        for (const QJsonValue &dimension : dimensions) {
            signal.dimensions.append(
                dimension.toInt()
                );
        }

        signal.role =
            determineSignalRole(signal, isInput);

        signalList.append(signal);
    }

    return signalList;
}


QString MainWindow::dimensionText(
    const QList<int> &dimensions) const
{
    QStringList parts;

    for (const int dimension : dimensions) {
        parts.append(
            QString::number(dimension)
            );
    }

    return parts.join(
        QStringLiteral(" × ")
        );
}


void MainWindow::fillSignalTable(
    QTableWidget *table,
    const QList<SignalInfo> &signalList)
{
    table->setRowCount(signalList.size());

    for (int row = 0;
         row < signalList.size();
         ++row) {
        const SignalInfo &signal =
            signalList.at(row);

        table->setItem(
            row,
            0,
            new QTableWidgetItem(signal.name)
            );

        table->setItem(
            row,
            1,
            new QTableWidgetItem(signal.dataType)
            );

        table->setItem(
            row,
            2,
            new QTableWidgetItem(signal.cType)
            );

        table->setItem(
            row,
            3,
            new QTableWidgetItem(
                dimensionText(signal.dimensions)
                )
            );
    }
}


bool MainWindow::isNumericSignal(
    const SignalInfo &signal) const
{
    return signal.dataType == QStringLiteral("single")
    || signal.dataType == QStringLiteral("double")
        || signal.dataType == QStringLiteral("int8")
        || signal.dataType == QStringLiteral("uint8")
        || signal.dataType == QStringLiteral("int16")
        || signal.dataType == QStringLiteral("uint16")
        || signal.dataType == QStringLiteral("int32")
        || signal.dataType == QStringLiteral("uint32");
}


bool MainWindow::hasDimensions(
    const SignalInfo &signal,
    int rows,
    int columns) const
{
    if (signal.dimensions.size() != 2) {
        return false;
    }

    return signal.dimensions.at(0) == rows
           && signal.dimensions.at(1) == columns;
}


MainWindow::SignalRole MainWindow::determineSignalRole(
    const SignalInfo &signal,
    bool isInput) const
{
    if (isInput) {
        if (isNumericSignal(signal)
            && hasDimensions(signal, 1601, 1)) {
            return SignalRole::Lidar;
        }

        if (isNumericSignal(signal)
            && hasDimensions(signal, 1, 1)) {
            return SignalRole::Scalar;
        }

        return SignalRole::Unknown;
    }

    return SignalRole::Monitoring;
}


QList<MainWindow::SignalInfo>
MainWindow::monitoringSignals(
    const QList<SignalInfo> &signalList) const
{
    QList<SignalInfo> filteredSignals;

    for (const SignalInfo &signal : signalList) {
        if (signal.role == SignalRole::Monitoring) {
            filteredSignals.append(signal);
        }
    }

    return filteredSignals;
}


void MainWindow::fillSignalComboBox(
    QComboBox *comboBox,
    const QList<SignalInfo> &signalList,
    int requiredRows,
    int requiredColumns)
{
    comboBox->clear();

    comboBox->addItem(
        QStringLiteral("Nicht zugeordnet")
        );

    for (const SignalInfo &signal : signalList) {
        if (!isNumericSignal(signal)) {
            continue;
        }

        if (!hasDimensions(
                signal,
                requiredRows,
                requiredColumns)) {
            continue;
        }

        comboBox->addItem(signal.name);
    }

    comboBox->setEnabled(
        comboBox->count() > 1
        );
}


void MainWindow::addMonitoringSignalRow()
{
    invalidateValidation();

    auto *rowWidget =
        new QWidget(this);

    auto *layout =
        new QHBoxLayout(rowWidget);

    layout->setContentsMargins(
        0,
        0,
        0,
        0
        );

    auto *editName =
        new QLineEdit;

    editName->setPlaceholderText(
        QStringLiteral("Anzeigename")
        );

    auto *comboSignal =
        new QComboBox;

    comboSignal->addItem(
        QStringLiteral("Nicht zugeordnet")
        );

    const QList<SignalInfo> availableSignals =
        monitoringSignals(outputSignals);

    for (const SignalInfo &signal : availableSignals) {
        comboSignal->addItem(signal.name);
    }

    comboSignal->setEnabled(
        comboSignal->count() > 1
        );

    auto *editUnit =
        new QLineEdit;

    editUnit->setPlaceholderText(
        QStringLiteral("Einheit")
        );

    auto *buttonRemove =
        new QPushButton(
            QStringLiteral("✕")
            );

    connect(
        buttonRemove,
        &QPushButton::clicked,
        this,
        [this, rowWidget]()
        {
            invalidateValidation();

            for (int index = 0;
                 index < monitoringRows.size();
                 ++index) {
                if (monitoringRows.at(index).rowWidget
                    == rowWidget) {
                    monitoringRows.removeAt(index);
                    break;
                }
            }

            ui->monitoringRowsLayout
                ->removeWidget(rowWidget);

            rowWidget->deleteLater();
        }
        );

    layout->addWidget(editName);
    layout->addWidget(comboSignal);
    layout->addWidget(editUnit);
    layout->addWidget(buttonRemove);

    ui->monitoringRowsLayout->insertWidget(
        ui->monitoringRowsLayout->count() - 1,
        rowWidget
        );

    MonitoringRow row;

    row.rowWidget = rowWidget;
    row.editName = editName;
    row.comboSignal = comboSignal;
    row.editUnit = editUnit;

    connect(
        editName,
        &QLineEdit::textChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        comboSignal,
        &QComboBox::currentIndexChanged,
        this,
        &MainWindow::invalidateValidation
        );

    connect(
        editUnit,
        &QLineEdit::textChanged,
        this,
        &MainWindow::invalidateValidation
        );

    monitoringRows.append(row);
}


void MainWindow::clearMonitoringRows()
{
    for (const MonitoringRow &row : monitoringRows) {
        if (!row.rowWidget) {
            continue;
        }

        ui->monitoringRowsLayout
            ->removeWidget(row.rowWidget);

        row.rowWidget->deleteLater();
    }

    monitoringRows.clear();
}


void MainWindow::invalidateValidation()
{
    ui->buttonStart->setEnabled(false);

    ui->statusBar->showMessage(
        QStringLiteral(
            "Konfiguration geändert – erneute "
            "Validierung erforderlich."
            ),
        3000
        );
}


void MainWindow::selectAlgorithmPackage()
{
    const QString selectedDirectory =
        QFileDialog::getExistingDirectory(
            this,
            QStringLiteral(
                "Algorithmuspaket auswählen"
                )
            );

    if (selectedDirectory.isEmpty()) {
        return;
    }

    algorithmTimer.stop();

    if (algorithmRunning && terminateFunction) {
        terminateFunction();
    }

    algorithmRunning = false;

    initializeFunction = nullptr;
    stepFunction = nullptr;
    terminateFunction = nullptr;

    if (algorithmLibrary.isLoaded()) {
        algorithmLibrary.unload();
    }

    clearMonitoringRows();
    invalidateValidation();

    ui->checkLidarReceiveEnabled->setChecked(false);
    ui->checkSteeringReceiveEnabled->setChecked(false);
    ui->checkMotorReceiveEnabled->setChecked(false);
    ui->checkCommandSendEnabled->setChecked(false);

    const QString manifestPath =
        selectedDirectory
        + QStringLiteral("/manifest.json");

    QFile manifestFile(manifestPath);

    if (!manifestFile.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Manifest nicht gefunden"),
            QStringLiteral(
                "Im ausgewählten Ordner wurde keine "
                "lesbare manifest.json gefunden."
                )
            );
        return;
    }

    QJsonParseError parseError;

    const QJsonDocument document =
        QJsonDocument::fromJson(
            manifestFile.readAll(),
            &parseError
            );

    manifestFile.close();

    if (parseError.error
            != QJsonParseError::NoError
        || !document.isObject()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Ungültiges Manifest"),
            QStringLiteral(
                "Die manifest.json konnte nicht "
                "gelesen werden.\n\nFehler: %1"
                ).arg(parseError.errorString())
            );
        return;
    }

    const QJsonObject manifest =
        document.object();

    inputSignals =
        readSignals(
            manifest.value(
                        QStringLiteral("inputs")
                        ).toArray(),
            true
            );

    outputSignals =
        readSignals(
            manifest.value(
                        QStringLiteral("outputs")
                        ).toArray(),
            false
            );

    fillSignalTable(
        ui->tableDetectedInputs,
        inputSignals
        );

    fillSignalTable(
        ui->tableDetectedOutputs,
        outputSignals
        );

    fillSignalTable(
        ui->tableAvailableOutputs,
        monitoringSignals(outputSignals)
        );

    fillSignalComboBox(
        ui->comboLidarX,
        inputSignals,
        1601,
        1
        );

    fillSignalComboBox(
        ui->comboLidarY,
        inputSignals,
        1601,
        1
        );

    fillSignalComboBox(
        ui->comboSteeringIn,
        inputSignals,
        1,
        1
        );

    fillSignalComboBox(
        ui->comboMotorIn,
        inputSignals,
        1,
        1
        );

    fillSignalComboBox(
        ui->comboSteeringOut,
        outputSignals,
        1,
        1
        );

    fillSignalComboBox(
        ui->comboMotorOut,
        outputSignals,
        1,
        1
        );

    const QString modelName =
        manifest.value(
                    QStringLiteral("modelName")
                    ).toString();

    const QJsonObject library =
        manifest.value(
                    QStringLiteral("library")
                    ).toObject();

    const QString libraryFileName =
        library.value(
                   QStringLiteral("fileName")
                   ).toString();

    if (modelName.isEmpty()
        || libraryFileName.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "Unvollständiges Manifest"
                ),
            QStringLiteral(
                "Im Manifest fehlen modelName "
                "oder library.fileName."
                )
            );
        return;
    }

    const QString libraryPath =
        selectedDirectory
        + QStringLiteral("/")
        + libraryFileName;

    if (!QFileInfo::exists(libraryPath)) {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "Bibliothek nicht gefunden"
                ),
            QStringLiteral(
                "Die im Manifest angegebene "
                "Bibliothek wurde nicht gefunden:"
                "\n\n%1"
                ).arg(libraryPath)
            );
        return;
    }

    algorithmPackagePath = selectedDirectory;
    algorithmLibraryPath = libraryPath;
    algorithmModelName = modelName;

    ui->labelAlgorithm->setText(
        QStringLiteral("Algorithmus: %1")
            .arg(algorithmModelName)
        );

    ui->labelAlgorithm->setToolTip(
        QStringLiteral(
            "Paket: %1\nBibliothek: %2"
            ).arg(
                algorithmPackagePath,
                algorithmLibraryPath
                )
        );

    ui->buttonValidate->setEnabled(true);
    ui->buttonStart->setEnabled(false);
    ui->buttonStop->setEnabled(false);

    ui->statusBar->showMessage(
        QStringLiteral(
            "Algorithmuspaket geladen: %1"
            ).arg(algorithmPackagePath),
        5000
        );
}


bool MainWindow::loadAlgorithmLibrary(
    QString &errorMessage)
{
    errorMessage.clear();

    if (algorithmLibraryPath.isEmpty()) {
        errorMessage =
            QStringLiteral(
                "Es wurde keine Bibliothek ausgewählt."
                );
        return false;
    }

    if (algorithmLibrary.isLoaded()) {
        algorithmLibrary.unload();
    }

    algorithmLibrary.setFileName(
        algorithmLibraryPath
        );

    if (!algorithmLibrary.load()) {
        errorMessage =
            algorithmLibrary.errorString();
        return false;
    }

    return true;
}


bool MainWindow::validateAlgorithmLibrary(
    QStringList &errors)
{
    errors.clear();

    initializeFunction = nullptr;
    stepFunction = nullptr;
    terminateFunction = nullptr;

    QString libraryError;

    if (!loadAlgorithmLibrary(libraryError)) {
        errors.append(
            QStringLiteral(
                "Algorithmusbibliothek konnte "
                "nicht geladen werden."
                )
            );

        return false;
    }

    const QString initializeName =
        algorithmModelName
        + QStringLiteral("_initialize");

    const QString stepName =
        algorithmModelName
        + QStringLiteral("_step");

    const QString terminateName =
        algorithmModelName
        + QStringLiteral("_terminate");

    initializeFunction =
        reinterpret_cast<InitializeFunction>(
            algorithmLibrary.resolve(
                initializeName
                    .toLatin1()
                    .constData()
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
            algorithmLibrary.resolve(
                stepName
                    .toLatin1()
                    .constData()
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
            algorithmLibrary.resolve(
                terminateName
                    .toLatin1()
                    .constData()
                )
            );

    if (!terminateFunction) {
        errors.append(
            QStringLiteral(
                "Funktion %1 wurde nicht gefunden."
                ).arg(terminateName)
            );
    }

    return errors.isEmpty();
}


MainWindow::~MainWindow()
{
    algorithmTimer.stop();

    if (algorithmRunning && terminateFunction) {
        terminateFunction();
    }

    if (algorithmLibrary.isLoaded()) {
        algorithmLibrary.unload();
    }

    delete ui;
}