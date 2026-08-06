#ifndef VALIDATIONDIALOG_H
#define VALIDATIONDIALOG_H

#include <QDialog>
#include <QStringList>

class QTextBrowser;

namespace Ui {
class ValidationDialog;
}

struct ValidationData
{
    QStringList inputs;
    QStringList outputs;
    QStringList monitoring;
    QStringList warnings;
    QStringList errors;
};

class ValidationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ValidationDialog(QWidget *parent = nullptr);
    ~ValidationDialog();

    void setValidationData(const ValidationData &data);

private:
    void adjustBrowserHeight(QTextBrowser *browser);
    QString formatEntries(const QStringList &entries,
                          const QString &emptyText) const;

    Ui::ValidationDialog *ui;
};

#endif // VALIDATIONDIALOG_H