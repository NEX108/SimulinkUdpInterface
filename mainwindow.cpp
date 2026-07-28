#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "validationdialog.h"
#include "monitoringwindow.h"
#include "algorithmruntime.h"

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
#include <QHostAddress>
#include <QDebug>
#include <QTextBrowser>
#include <QTextCursor>
#include <QPlainTextEdit>
#include <QDateTime>
#include <QScrollBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    initializeOverviewPage();

    algorithmRuntime = new AlgorithmRuntime(this);

    logTimer.start();

    connect(
        ui->buttonClearLogs,
        &QPushButton::clicked,
        this,
        [this]()
        {
            ui->plainTextLog->clear();

            /*
         * Auch die Frequenzzeitpunkte zurücksetzen.
         * Der nächste eingehende Wert darf dadurch
         * sofort wieder angezeigt werden.
         */
            lastLogTimes.clear();
            logTimer.restart();
        }
        );

    appendLog(
        QStringLiteral("SYSTEM Tool geöffnet"),
        {
            QStringLiteral("Log-Frequenz: %1 Hz")
            .arg(ui->spinLogFrequency->value())
        }
        );

    connect(
        algorithmRuntime,
        &AlgorithmRuntime::lidarLogData,
        this,
        [this](
            qsizetype byteCount,
            quint32 pointCount,
            quint16 port)
        {
            if (!ui->checkLidarLog->isChecked()) {
                return;
            }

            if (!shouldWriteDynamicLog(
                    QStringLiteral("lidar"))) {
                return;
            }

            appendLog(
                QStringLiteral("RX LiDAR"),
                {
                    QStringLiteral(
                        "Port: %1 | Bytes: %2 | Punkte: %3"
                        )
                        .arg(port)
                        .arg(byteCount)
                        .arg(pointCount)
                }
                );
        }
        );

    connect(
        algorithmRuntime,
        &AlgorithmRuntime::motorActualLogData,
        this,
        [this](
            qsizetype byteCount,
            float value,
            quint16 port)
        {
            if (!ui->checkMotorActualLog->isChecked()) {
                return;
            }

            if (!shouldWriteDynamicLog(
                    QStringLiteral("motorActual"))) {
                return;
            }

            appendLog(
                QStringLiteral("RX Motor Ist"),
                {
                    QStringLiteral(
                        "Port: %1 | Bytes: %2 | Wert: %3"
                        )
                        .arg(port)
                        .arg(byteCount)
                        .arg(
                            static_cast<double>(value),
                            0,
                            'f',
                            3
                            )
                }
                );
        }
        );

    connect(
        algorithmRuntime,
        &AlgorithmRuntime::steeringActualLogData,
        this,
        [this](
            qsizetype byteCount,
            float value,
            quint16 port)
        {
            if (!ui->checkSteeringActualLog->isChecked()) {
                return;
            }

            if (!shouldWriteDynamicLog(
                    QStringLiteral("steeringActual"))) {
                return;
            }

            appendLog(
                QStringLiteral("RX Lenkwinkel Ist"),
                {
                    QStringLiteral(
                        "Port: %1 | Bytes: %2 | Wert: %3"
                        )
                        .arg(port)
                        .arg(byteCount)
                        .arg(
                            static_cast<double>(value),
                            0,
                            'f',
                            3
                            )
                }
                );
        }
        );

    connect(
        algorithmRuntime,
        &AlgorithmRuntime::commandLogData,
        this,
        [this](
            qsizetype byteCount,
            double steeringSetpoint,
            double motorSetpoint,
            quint16 port)
        {
            if (!ui->checkCommandLog->isChecked()) {
                return;
            }

            if (!shouldWriteDynamicLog(
                    QStringLiteral("command"))) {
                return;
            }

            appendLog(
                QStringLiteral("TX Steuerung Soll"),
                {
                    QStringLiteral(
                        "Port: %1 | Bytes: %2"
                        )
                        .arg(port)
                        .arg(byteCount),

                    QStringLiteral(
                        "Lenkwinkel Soll: %1 | Motor Soll: %2"
                        )
                        .arg(
                            steeringSetpoint,
                            0,
                            'f',
                            3
                            )
                        .arg(
                            motorSetpoint,
                            0,
                            'f',
                            3
                            )
                }
                );
        }
        );

    connect(
        algorithmRuntime,
        &AlgorithmRuntime::stepCompleted,
        this,
        [this](quint64 stepCount)
        {
            /*
         * Statusanzeige ungefähr einmal pro Sekunde
         * aktualisieren.
         */
            if (stepCount % 50 == 0) {
                ui->statusBar->showMessage(
                    QStringLiteral(
                        "Algorithmus läuft – Schritte: %1"
                        ).arg(stepCount)
                    );
            }
        }
        );

    connect(
        algorithmRuntime,
        &AlgorithmRuntime::runtimeError,
        this,
        [this](const QString &message)
        {
            qWarning() << "Runtime-Fehler:" << message;

            ui->statusBar->showMessage(
                QStringLiteral("Runtime-Fehler: %1")
                    .arg(message),
                5000
                );

            appendLog(
                QStringLiteral("ERROR Runtime-Fehler"),
                {
                    message
                }
                );
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

            algorithmRuntime->validate(libraryErrors);

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

            logValidationResult(
                data,
                validationAccepted
                );

            if (!validationAccepted) {
                algorithmRuntime->unload();
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
            UdpRuntimeConfiguration udpConfiguration;

            /*
             * LiDAR-Empfang
             */
            udpConfiguration.lidar.enabled =
                ui->checkLidarReceiveEnabled->isChecked();

            udpConfiguration.lidar.port =
                static_cast<quint16>(
                    ui->spinLidarReceivePort->value()
                    );

            /*
             * Lenkwinkel-Ist-Empfang
             */
            udpConfiguration.steering.enabled =
                ui->checkSteeringReceiveEnabled->isChecked();

            udpConfiguration.steering.port =
                static_cast<quint16>(
                    ui->spinSteeringReceivePort->value()
                    );

            /*
             * Motor-RPM-Empfang
             */
            udpConfiguration.motorRpm.enabled =
                ui->checkMotorReceiveEnabled->isChecked();

            udpConfiguration.motorRpm.port =
                static_cast<quint16>(
                    ui->spinMotorReceivePort->value()
                    );

            /*
             * Gemeinsamer Steuerungssender:
             * Lenkwinkel-Soll und Motor-Soll
             */
            udpConfiguration.command.enabled =
                ui->checkCommandSendEnabled->isChecked();

            udpConfiguration.command.address =
                QHostAddress(
                    ui->editCommandTargetIp
                        ->text()
                        .trimmed()
                    );

            udpConfiguration.command.port =
                static_cast<quint16>(
                    ui->spinCommandTargetPort->value()
                    );

            /*
             * Ausgewählte Simulink-Signalzuordnungen
             */
            udpConfiguration.lidarXInputSignal =
                ui->comboLidarX->currentIndex() > 0
                    ? ui->comboLidarX->currentText()
                    : QString();

            udpConfiguration.lidarYInputSignal =
                ui->comboLidarY->currentIndex() > 0
                    ? ui->comboLidarY->currentText()
                    : QString();

            udpConfiguration.steeringInputSignal =
                ui->comboSteeringIn->currentIndex() > 0
                    ? ui->comboSteeringIn->currentText()
                    : QString();

            udpConfiguration.motorInputSignal =
                ui->comboMotorIn->currentIndex() > 0
                    ? ui->comboMotorIn->currentText()
                    : QString();

            udpConfiguration.steeringOutputSignal =
                ui->comboSteeringOut->currentIndex() > 0
                    ? ui->comboSteeringOut->currentText()
                    : QString();

            udpConfiguration.motorOutputSignal =
                ui->comboMotorOut->currentIndex() > 0
                    ? ui->comboMotorOut->currentText()
                    : QString();

            /*
             * Index 0 entspricht "Immer senden".
             * In diesem Fall bleibt der Signalname leer.
             */
            udpConfiguration.commandEnableSignal =
                ui->comboCommandEnable->currentIndex() > 0
                    ? ui->comboCommandEnable->currentText()
                    : QString();

            QString errorMessage;

            if (!algorithmRuntime->start(
                    udpConfiguration,
                    errorMessage)) {
                QMessageBox::critical(
                    this,
                    QStringLiteral("Start fehlgeschlagen"),
                    errorMessage
                    );

                appendLog(
                    QStringLiteral("ERROR Ausführung konnte nicht gestartet werden"),
                    {
                        errorMessage
                    }
                    );

                return;
            }

            appendLog(
                QStringLiteral("SYSTEM Ausführung gestartet"),
                {
                    QStringLiteral("Log-Frequenz: %1 Hz")
                    .arg(ui->spinLogFrequency->value()),

                        QStringLiteral("LiDAR-Port: %1")
                            .arg(ui->spinLidarReceivePort->value()),

                        QStringLiteral("Motor-Ist-Port: %1")
                            .arg(ui->spinMotorReceivePort->value()),

                        QStringLiteral("Lenkwinkel-Ist-Port: %1")
                            .arg(ui->spinSteeringReceivePort->value()),

                        QStringLiteral("Command-Port: %1")
                            .arg(ui->spinCommandTargetPort->value())
                }
                );

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
            algorithmRuntime->stop();

            const quint64 stepCount =
                algorithmRuntime->stepCount();

            /*
         * Nach dem Stoppen wird die Bibliothek entladen.
         * Für einen erneuten Start muss erneut validiert werden.
         */
            algorithmRuntime->unload();

            ui->buttonStart->setEnabled(false);
            ui->buttonStop->setEnabled(false);

            ui->statusBar->showMessage(
                QStringLiteral(
                    "Algorithmus gestoppt – insgesamt %1 Schritte."
                    ).arg(stepCount)
                );

            appendLog(
                QStringLiteral("SYSTEM Ausführung gestoppt"),
                {
                    QStringLiteral("Algorithmusschritte: %1")
                    .arg(stepCount)
                }
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
        ui->liveMonitorPage,
        ui->recordsPage,
        ui->logsPage,
        ui->debugPage
    };

    connect(
        ui->navigationList,
        &QListWidget::itemClicked,
        this,
        [this, pages](QListWidgetItem *item)
        {
            if (!item) {
                return;
            }

            const int row = ui->navigationList->row(item);

            if (row < 0 || row >= pages.size()) {
                return;
            }

            if (pages.at(row) != ui->liveMonitorPage) {
                return;
            }

            if (!monitoringWindow) {
                monitoringWindow = new MonitoringWindow(this);

                connect(
                    algorithmRuntime,
                    &AlgorithmRuntime::stepCompleted,
                    monitoringWindow,
                    [this](quint64)
                    {
                        if (!monitoringWindow
                            || !algorithmRuntime) {
                            return;
                        }

                        double steeringSoll = 0.0;
                        double motorSoll = 0.0;
                        QString errorMessage;

                        if (ui->comboSteeringOut->currentIndex() > 0) {
                            algorithmRuntime->readOutputScalar(
                                ui->comboSteeringOut->currentText(),
                                steeringSoll,
                                errorMessage
                                );
                        }

                        errorMessage.clear();

                        if (ui->comboMotorOut->currentIndex() > 0) {
                            algorithmRuntime->readOutputScalar(
                                ui->comboMotorOut->currentText(),
                                motorSoll,
                                errorMessage
                                );
                        }

                        /*
                         * Die Istwerte werden über die UDP-Runtime aktualisiert.
                         */
                        monitoringWindow->setRuntimeValues(
                            steeringActual,
                            motorActual,
                            steeringSoll,
                            motorSoll
                            );

                        QVector<DiagnosticValue> diagnostics;

                        for (const MonitoringRow &monitoringRow
                             : monitoringRows) {
                            if (!monitoringRow.comboSignal
                                || monitoringRow.comboSignal
                                           ->currentIndex() <= 0) {
                                continue;
                            }

                            const QString signalName =
                                monitoringRow.comboSignal
                                    ->currentText();

                            QString displayName =
                                monitoringRow.editName
                                    ? monitoringRow.editName
                                          ->text()
                                          .trimmed()
                                    : QString();

                            if (displayName.isEmpty()) {
                                displayName = signalName;
                            }

                            const QString unit =
                                monitoringRow.editUnit
                                    ? monitoringRow.editUnit
                                          ->text()
                                          .trimmed()
                                    : QString();

                            double value = 0.0;
                            QString readError;

                            if (!algorithmRuntime->readOutputScalar(
                                    signalName,
                                    value,
                                    readError)) {
                                continue;
                            }

                            bool signalIsBoolean = false;

                            for (const SignalInfo &signal : outputSignals) {
                                if (signal.name == signalName) {
                                    signalIsBoolean = isBooleanSignal(signal);
                                    break;
                                }
                            }

                            diagnostics.append({
                                displayName,
                                unit,
                                value,
                                signalIsBoolean
                            });
                        }

                        monitoringWindow->setDiagnosticValues(
                            diagnostics
                            );
                    }
                    );

                connect(
                    algorithmRuntime,
                    &AlgorithmRuntime::lidarDataUpdated,
                    monitoringWindow,
                    &MonitoringWindow::setLidarPoints);

                connect(
                    algorithmRuntime,
                    &AlgorithmRuntime::steeringActualUpdated,
                    this,
                    [this](float value)
                    {
                        steeringActual =
                            static_cast<double>(value);
                    });

                connect(
                    algorithmRuntime,
                    &AlgorithmRuntime::motorRpmUpdated,
                    this,
                    [this](float value)
                    {
                        motorActual =
                            static_cast<double>(value);
                    });
                }

            /*
             * Bis UDP eingebaut ist, gibt es keine künstliche
             * LiDAR-Punktwolke mehr.
             */

            monitoringWindow->setRuntimeValues(
                0.0,
                0.0,
                0.0,
                0.0
                );

            monitoringWindow->show();
            monitoringWindow->raise();
            monitoringWindow->activateWindow();

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

            QWidget *selectedPage = pages.at(row);

            /*
         * Der Live-Monitor wird als separates Fenster geöffnet
         * und nicht im QStackedWidget angezeigt.
         */
            if (selectedPage == ui->liveMonitorPage) {
                return;
            }

            ui->contentStack->setCurrentWidget(selectedPage);

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

    connect(
        ui->comboCommandEnable,
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

void MainWindow::logValidationResult(
    const ValidationData &data,
    bool accepted)
{
    QStringList details;

    details.append(
        QStringLiteral("Fehler: %1")
            .arg(data.errors.size())
        );

    details.append(
        QStringLiteral("Warnungen: %1")
            .arg(data.warnings.size())
        );

    for (const QString &error : data.errors) {
        details.append(
            QStringLiteral("ERROR: %1").arg(error)
            );
    }

    for (const QString &warning : data.warnings) {
        details.append(
            QStringLiteral("WARNING: %1").arg(warning)
            );
    }

    for (const QString &input : data.inputs) {
        details.append(
            QStringLiteral("Eingang: %1").arg(input)
            );
    }

    for (const QString &output : data.outputs) {
        details.append(
            QStringLiteral("Ausgang: %1").arg(output)
            );
    }

    for (const QString &monitoring : data.monitoring) {
        details.append(
            QStringLiteral("Diagnose: %1").arg(monitoring)
            );
    }

    appendLog(
        accepted
            ? QStringLiteral("VALIDATION erfolgreich")
            : QStringLiteral("VALIDATION fehlgeschlagen"),
        details
        );
}

void MainWindow::initializeOverviewPage()
{
    /*
     * Der QTextBrowser dient ausschließlich als statische
     * Informations- und Hilfeseite.
     */
    ui->overviewTextBrowser->setReadOnly(true);
    ui->overviewTextBrowser->setOpenExternalLinks(false);

    /*
     * Optische Anpassung des äußeren QTextBrowser-Widgets.
     * Der eigentliche Inhalt wird weiter unten über HTML gestaltet.
     */
    ui->overviewTextBrowser->setStyleSheet(
        QStringLiteral(
            "QTextBrowser {"
            "    background-color: transparent;"
            "    border: none;"
            "    padding: 0px;"
            "}"
            )
        );

    const QString overviewHtml = QStringLiteral(R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">

    <style>
        body {
            font-family: "Segoe UI", "Arial", sans-serif;
            font-size: 10.5pt;
            line-height: 1.55;
            color: #263238;
            background-color: transparent;
            margin: 22px 30px 36px 30px;
        }

        h1 {
            font-size: 24pt;
            font-weight: 600;
            color: #1f2933;
            margin-top: 0px;
            margin-bottom: 8px;
        }

        h2 {
            font-size: 16pt;
            font-weight: 600;
            color: #1f4e79;
            margin-top: 28px;
            margin-bottom: 10px;
            padding-bottom: 5px;
            border-bottom: 1px solid #c8d2dc;
        }

        h3 {
            font-size: 12.5pt;
            font-weight: 600;
            color: #263238;
            margin-top: 20px;
            margin-bottom: 6px;
        }

        p {
            margin-top: 5px;
            margin-bottom: 10px;
        }

        ul {
            margin-top: 6px;
            margin-bottom: 12px;
            margin-left: 18px;
        }

        li {
            margin-bottom: 4px;
        }

        .intro {
            font-size: 11.5pt;
            color: #455a64;
            margin-bottom: 12px;
        }

        .workflow-step {
            background-color: #f5f7f9;
            border: 1px solid #dce3e8;
            border-radius: 5px;
            margin-top: 10px;
            margin-bottom: 12px;
            padding: 10px 14px;
        }

        .workflow-step h3 {
            color: #1f4e79;
            margin-top: 0px;
        }

        .signal-flow {
            background-color: #eef4f8;
            border: 1px solid #cbd9e3;
            border-radius: 5px;
            margin-top: 10px;
            padding: 16px;
            text-align: center;
            font-family: "Consolas", "Courier New", monospace;
            font-size: 10.5pt;
            color: #263238;
        }

        .arrow {
            color: #1f4e79;
            font-size: 14pt;
            font-weight: bold;
        }

        .notice {
            background-color: #fff8e1;
            border: 1px solid #e4d49b;
            border-radius: 5px;
            margin-top: 12px;
            padding: 12px 14px;
        }

        .value-box {
            background-color: #f5f7f9;
            border: 1px solid #d5dde3;
            border-radius: 4px;
            margin-top: 8px;
            margin-bottom: 8px;
            padding: 9px 12px;
            font-family: "Consolas", "Courier New", monospace;
            font-weight: bold;
            color: #1f4e79;
        }

        .footer-note {
            color: #546e7a;
            margin-top: 14px;
        }
    </style>
</head>

<body>

    <h1>Übersicht</h1>

    <p class="intro">
        Dieses Tool dient zur Integration und Ausführung von
        Steuerungs- und Fahrerassistenzalgorithmen, die in
        MATLAB/Simulink entwickelt wurden.
    </p>

    <p>
        Die Algorithmen können zunächst mit einem virtuellen Fahrzeug
        in Unreal Engine 5 getestet und anschließend für den Betrieb
        auf dem realen RC-Fahrzeug verwendet werden.
    </p>

    <h2>Typischer Arbeitsablauf</h2>

    <div class="workflow-step">
        <h3>1. Simulink-Algorithmus entwickeln</h3>

        <p>
            Der Algorithmus wird in einem Simulink-Modell erstellt.
            Als Eingänge können beispielsweise LiDAR-Daten,
            Lenkwinkel oder Motordrehzahl verwendet werden.
        </p>

        <p>
            Als Ausgänge werden die Sollwerte für Lenkung und Antrieb
            sowie optional weitere Diagnosesignale definiert.
        </p>
    </div>

    <div class="workflow-step">
        <h3>2. Code generieren</h3>

        <p>
            Das Simulink-Modell wird mit Simulink Coder als
            C- oder C++-Bibliothek erzeugt. Die generierten Dateien
            werden anschließend als Algorithmuspaket in dieses Tool
            geladen.
        </p>
    </div>

    <div class="workflow-step">
        <h3>3. Signale zuordnen</h3>

        <p>
            Nach dem Laden des Algorithmus müssen die erkannten
            Ein- und Ausgänge den jeweiligen Funktionen des Tools
            zugeordnet werden.
        </p>

        <ul>
            <li>LiDAR-X- und LiDAR-Y-Daten</li>
            <li>Lenkwinkel-Istwert</li>
            <li>Motor-Istwert</li>
            <li>Lenkwinkel-Sollwert</li>
            <li>Motor-Sollwert</li>
            <li>optionale Diagnosesignale</li>
        </ul>
    </div>

    <div class="workflow-step">
        <h3>4. Konfiguration validieren</h3>

        <p>
            Vor der Ausführung prüft das Tool, ob die Bibliothek
            geladen werden kann und ob alle erforderlichen Signale
            vollständig und eindeutig zugeordnet wurden.
        </p>

        <p>
            Fehler und Hinweise werden vor dem Start in einer
            Validierungsübersicht angezeigt.
        </p>
    </div>

    <div class="workflow-step">
        <h3>5. Simulation starten</h3>

        <p>
            Der Algorithmus wird mit der Unreal-Engine-Simulation
            verbunden. Die simulierten Sensordaten werden an den
            Algorithmus übergeben. Seine berechneten Ausgangswerte
            werden anschließend an das virtuelle Fahrzeug gesendet.
        </p>
    </div>

    <div class="workflow-step">
        <h3>6. Verhalten überwachen</h3>

        <p>
            Im Live-Monitor können während der Ausführung unter
            anderem folgende Informationen betrachtet werden:
        </p>

        <ul>
            <li>aktuelle LiDAR-Punktwolke</li>
            <li>skalare Eingangssignale</li>
            <li>berechnete Lenk- und Motorsollwerte</li>
            <li>ausgewählte Diagnosesignale</li>
        </ul>
    </div>

    <h2>Signalfluss der Simulation</h2>

    <div class="signal-flow">
        <strong>Unreal Engine 5</strong><br>
        <span class="arrow">↓</span><br>
        Simulierte Sensordaten<br>
        <span class="arrow">↓</span><br>
        Geladener Simulink-Algorithmus<br>
        <span class="arrow">↓</span><br>
        Lenk- und Motor-Sollwerte<br>
        <span class="arrow">↓</span><br>
        <strong>Virtuelles Fahrzeug</strong>
    </div>

    <p class="footer-note">
        Beim späteren Einsatz am realen Fahrzeug werden die
        simulierten Sensoren und Aktoren durch die reale
        Fahrzeugschnittstelle ersetzt.
    </p>

    <h2>Wichtige Hinweise</h2>

    <div class="notice">
        <p>
            Der Algorithmus muss in jedem Ausführungsschritt gültige
            Ausgangswerte bereitstellen. Lenk- und Motorwerte müssen
            innerhalb der vorgesehenen Wertebereiche liegen.
        </p>

        <p>
            Die Begrenzung der Ausgangswerte durch das Tool ersetzt
            keine korrekte Regelungs- und Sicherheitslogik innerhalb
            des Algorithmus.
        </p>
    </div>

    <h3>Notbremsfunktion</h3>

    <p>
        Eine Notbremsfunktion setzt den Motor-Sollwert auf den
        für das System definierten neutralen Wert:
    </p>

    <div class="value-box">
        Motor-Sollwert 0 = neutral beziehungsweise bremsen
    </div>

    <p>
        Ein zugehöriges Diagnosesignal kann beispielsweise anzeigen,
        ob die Notbremsung derzeit aktiv ist:
    </p>

    <div class="value-box">
        emergency_active: 0 = inaktiv, 1 = aktiv
    </div>

    <div class="notice">
        <strong>Sicherheitshinweis:</strong>

        <p>
            Vor dem Einsatz am realen Fahrzeug sollte ein Algorithmus
            vollständig in der Simulation geprüft werden.
        </p>
    </div>

</body>
</html>
)");

    ui->overviewTextBrowser->setHtml(overviewHtml);

    /*
     * Nach dem Einfügen des Inhalts an den Seitenanfang springen.
     */
    ui->overviewTextBrowser->moveCursor(
        QTextCursor::Start
        );
}

void MainWindow::appendLog(
    const QString &title,
    const QStringList &details)
{
    const QString timestamp =
        QDateTime::currentDateTime()
            .toString(QStringLiteral("HH:mm:ss.zzz"));

    QString entry =
        QStringLiteral("[%1] %2")
            .arg(timestamp, title);

    for (const QString &detail : details) {
        if (!detail.trimmed().isEmpty()) {
            entry += QStringLiteral("\n%1").arg(detail);
        }
    }

    ui->plainTextLog->appendPlainText(entry);
    ui->plainTextLog->appendPlainText(QString());

    /*
     * Automatisch zum neuesten Eintrag scrollen.
     */
    QScrollBar *scrollBar =
        ui->plainTextLog->verticalScrollBar();

    scrollBar->setValue(scrollBar->maximum());
}

bool MainWindow::shouldWriteDynamicLog(
    const QString &category)
{
    if (!logTimer.isValid()) {
        logTimer.start();
    }

    const int frequencyHz =
        qMax(1, ui->spinLogFrequency->value());

    const qint64 minimumIntervalMs =
        1000 / frequencyHz;

    const qint64 currentTimeMs =
        logTimer.elapsed();

    const qint64 lastTimeMs =
        lastLogTimes.value(category, -minimumIntervalMs);

    if (currentTimeMs - lastTimeMs
        < minimumIntervalMs) {
        return false;
    }

    lastLogTimes.insert(
        category,
        currentTimeMs
        );

    return true;
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

        signal.elementCount =
            static_cast<qsizetype>(
                object.value(
                          QStringLiteral("elementCount")
                          ).toDouble(0.0)
                );

        if (signal.elementCount <= 0) {
            signal.elementCount = 1;

            for (const int dimension : signal.dimensions) {
                signal.elementCount *= dimension;
            }
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

bool MainWindow::isBooleanSignal(
    const SignalInfo &signal) const
{
    return signal.dataType.compare(
               QStringLiteral("boolean"),
               Qt::CaseInsensitive) == 0
           || signal.dataType.compare(
                  QStringLiteral("bool"),
                  Qt::CaseInsensitive) == 0
           || signal.cType.compare(
                  QStringLiteral("boolean_T"),
                  Qt::CaseInsensitive) == 0
           || signal.cType.compare(
                  QStringLiteral("bool"),
                  Qt::CaseInsensitive) == 0;
}

MainWindow::SignalRole MainWindow::determineSignalRole(
    const SignalInfo &signal,
    bool isInput) const
{
    /*
     * Eingänge für LiDAR, Lenkwinkel und Motor müssen weiterhin
     * numerisch sein. Boolean-Eingänge werden hier nicht angeboten.
     */
    if (isInput) {
        if (!isNumericSignal(signal)) {
            return SignalRole::Unknown;
        }

        return signal.elementCount == 1
                   ? SignalRole::Scalar
                   : SignalRole::Lidar;
    }

    /*
     * Diagnosesignale dürfen numerische Skalare oder Boolean-Skalare sein.
     */
    if (signal.elementCount == 1
        && (isNumericSignal(signal)
            || isBooleanSignal(signal))) {
        return SignalRole::Monitoring;
    }

    /*
     * Auch numerische Ausgangsvektoren bleiben grundsätzlich
     * als Monitoring-Ausgänge bekannt.
     */
    if (isNumericSignal(signal)) {
        return SignalRole::Monitoring;
    }

    return SignalRole::Unknown;
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
    SignalRole requiredRole)
{
    comboBox->clear();

    comboBox->addItem(
        QStringLiteral("Nicht zugeordnet")
        );

    for (const SignalInfo &signal : signalList) {
        if (!isNumericSignal(signal)) {
            continue;
        }

        bool accepted = false;

        switch (requiredRole) {
        case SignalRole::Lidar:
            accepted = signal.elementCount > 1;
            break;

        case SignalRole::Scalar:
            accepted = signal.elementCount == 1;
            break;

        case SignalRole::Monitoring:
            accepted = true;
            break;

        case SignalRole::Unknown:
            accepted = false;
            break;
        }

        if (accepted) {
            comboBox->addItem(signal.name);
        }
    }

    comboBox->setEnabled(
        comboBox->count() > 1
        );
}

void MainWindow::fillCommandEnableComboBox(
    QComboBox *comboBox,
    const QList<SignalInfo> &signalList)
{
    comboBox->clear();

    /*
     * Index 0 bedeutet:
     * Das UDP-Steuerpaket wird in jedem Zyklus gesendet.
     */
    comboBox->addItem(
        QStringLiteral("Immer senden")
        );

    /*
     * Nur skalare Bool-Ausgänge anbieten.
     */
    for (const SignalInfo &signal : signalList) {
        if (!isBooleanSignal(signal)) {
            continue;
        }

        if (signal.elementCount != 1) {
            continue;
        }

        comboBox->addItem(signal.name);
    }

    /*
     * "Immer senden" steht immer zur Verfügung.
     */
    comboBox->setEnabled(true);
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

    algorithmRuntime->unload();

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
        SignalRole::Lidar
        );

    fillSignalComboBox(
        ui->comboLidarY,
        inputSignals,
        SignalRole::Lidar
        );

    fillSignalComboBox(
        ui->comboSteeringIn,
        inputSignals,
        SignalRole::Scalar
        );

    fillSignalComboBox(
        ui->comboMotorIn,
        inputSignals,
        SignalRole::Scalar
        );

    fillSignalComboBox(
        ui->comboSteeringOut,
        outputSignals,
        SignalRole::Scalar
        );

    fillSignalComboBox(
        ui->comboMotorOut,
        outputSignals,
        SignalRole::Scalar
        );

    fillCommandEnableComboBox(
        ui->comboCommandEnable,
        outputSignals
        );

    algorithmFolder = selectedDirectory;

    algorithmRuntime->configure(algorithmFolder);

    ui->labelAlgorithm->setText(
        QStringLiteral("Algorithmus: %1")
            .arg(algorithmRuntime->modelName()));

    ui->labelAlgorithm->setToolTip(
        QStringLiteral(
            "Paket: %1\nBibliothek: %2"
            ).arg(
                algorithmFolder,
                algorithmRuntime->libraryPath()
                )
        );

    ui->buttonValidate->setEnabled(true);
    ui->buttonStart->setEnabled(false);
    ui->buttonStop->setEnabled(false);

    ui->statusBar->showMessage(
        QStringLiteral(
            "Algorithmuspaket geladen: %1"
            ).arg(algorithmFolder),
        5000
        );
}

MainWindow::~MainWindow()
{
    delete ui;
}