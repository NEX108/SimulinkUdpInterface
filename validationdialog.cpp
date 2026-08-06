#include "validationdialog.h"
#include "ui_validationdialog.h"

#include <QAbstractTextDocumentLayout>
#include <QGroupBox>
#include <QLayout>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>

ValidationDialog::ValidationDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ValidationDialog)
{
    ui->setupUi(this);

    const QString transparentBrowserStyle =
        QStringLiteral(
            "QTextBrowser {"
            "    background: transparent;"
            "    border: none;"
            "    padding: 0px;"
            "}"
            );

    const QList<QTextBrowser *> browsers{
        ui->browserValidationInputs,
        ui->browserValidationOutputs,
        ui->browserValidationMonitoring,
        ui->browserValidationWarnings,
        ui->browserValidationErrors
    };

    for (QTextBrowser *browser : browsers)
    {
        browser->setStyleSheet(transparentBrowserStyle);

        browser->setSizePolicy(
            QSizePolicy::Expanding,
            QSizePolicy::Minimum
            );

        browser->setMinimumHeight(0);

        browser->setSizeAdjustPolicy(
            QAbstractScrollArea::AdjustToContents
            );

        // Die äußere ScrollArea übernimmt später das Scrollen.
        browser->setVerticalScrollBarPolicy(
            Qt::ScrollBarAlwaysOff
            );

        browser->setHorizontalScrollBarPolicy(
            Qt::ScrollBarAlwaysOff
            );
    }

    const QList<QGroupBox *> groupBoxes{
        ui->groupValidationInputs,
        ui->groupValidationOutputs,
        ui->groupValidationMonitoring,
        ui->groupValidationWarnings,
        ui->groupValidationErrors
    };

    for (QGroupBox *groupBox : groupBoxes)
    {
        groupBox->setSizePolicy(
            QSizePolicy::Expanding,
            QSizePolicy::Minimum
            );

        groupBox->setMinimumHeight(0);

        if (groupBox->layout())
        {
            groupBox->layout()->setContentsMargins(8, 8, 8, 8);
            groupBox->layout()->setSpacing(0);
            groupBox->layout()->setSizeConstraint(
                QLayout::SetMinimumSize
                );
        }
    }

    if (ui->validationScrollContent->layout())
    {
        ui->validationScrollContent
            ->layout()
            ->setSizeConstraint(QLayout::SetMinimumSize);
    }
}

QString ValidationDialog::formatEntries(
    const QStringList &entries,
    const QString &emptyText) const
{
    if (entries.isEmpty()) {
        return QStringLiteral(
                   "<span style='color: orange;'>%1</span>"
                   ).arg(emptyText.toHtmlEscaped());
    }

    QStringList htmlEntries;

    for (const QString &entry : entries) {
        htmlEntries.append(entry.toHtmlEscaped());
    }

    return htmlEntries.join(QStringLiteral("<br>"));
}

void ValidationDialog::setValidationData(
    const ValidationData &data)
{
    ui->browserValidationInputs->setHtml(
        formatEntries(
            data.inputs,
            QStringLiteral("Keine Eingänge konfiguriert.")
            )
        );

    ui->browserValidationOutputs->setHtml(
        formatEntries(
            data.outputs,
            QStringLiteral("Keine Ausgänge konfiguriert.")
            )
        );

    ui->browserValidationMonitoring->setHtml(
        formatEntries(
            data.monitoring,
            QStringLiteral("Keine Diagnosesignale konfiguriert.")
            )
        );

    const QString warningsHtml =
        formatEntries(
            data.warnings,
            QStringLiteral("Keine Hinweise vorhanden.")
            );

    ui->browserValidationWarnings->setHtml(
        data.warnings.isEmpty()
            ? warningsHtml
            : QStringLiteral(
                  "<span style='color: orange;'>%1</span>"
                  ).arg(warningsHtml)
        );

    const QString errorsHtml =
        formatEntries(
            data.errors,
            QStringLiteral("Keine Fehler gefunden.")
            );

    ui->browserValidationErrors->setHtml(
        data.errors.isEmpty()
            ? QStringLiteral(
                  "<span style='color: green;'>%1</span>"
                  ).arg(QStringLiteral("Keine Fehler gefunden."))
            : QStringLiteral(
                  "<span style='color: red;'>%1</span>"
                  ).arg(errorsHtml)
        );

    QTimer::singleShot(
        0,
        this,
        [this]()
        {
            adjustBrowserHeight(ui->browserValidationInputs);
            adjustBrowserHeight(ui->browserValidationOutputs);
            adjustBrowserHeight(ui->browserValidationMonitoring);
            adjustBrowserHeight(ui->browserValidationWarnings);
            adjustBrowserHeight(ui->browserValidationErrors);

            const QList<QGroupBox *> groupBoxes{
                ui->groupValidationInputs,
                ui->groupValidationOutputs,
                ui->groupValidationMonitoring,
                ui->groupValidationWarnings,
                ui->groupValidationErrors
            };

            for (QGroupBox *groupBox : groupBoxes)
            {
                if (groupBox->layout()) {
                    groupBox->layout()->activate();
                }

                groupBox->adjustSize();
            }

            if (ui->validationScrollContent->layout()) {
                ui->validationScrollContent
                    ->layout()
                    ->activate();
            }

            ui->validationScrollContent->adjustSize();

            if (layout()) {
                layout()->activate();
            }

            adjustSize();
        }
        );
}

void ValidationDialog::adjustBrowserHeight(QTextBrowser *browser)
{
    if (!browser)
    {
        return;
    }

    QTextDocument *document = browser->document();

    /*
     * Die verfügbare Breite berücksichtigen, damit Zeilenumbrüche
     * in die Höhenberechnung eingehen.
     */
    const int availableWidth =
        qMax(browser->viewport()->width(), 100);

    document->setTextWidth(availableWidth);

    const qreal documentHeight =
        document->documentLayout()->documentSize().height();

    const int frameHeight =
        browser->frameWidth() * 2;

    const int browserHeight =
        static_cast<int>(documentHeight)
        + frameHeight
        + 4;

    browser->setMinimumHeight(browserHeight);
    browser->setMaximumHeight(browserHeight);
}

ValidationDialog::~ValidationDialog()
{
    delete ui;
}