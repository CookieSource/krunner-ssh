#include "manualentrydialog.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>

ManualEntryDialog::ManualEntryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Add SSH Entry"));
    auto *layout = new QFormLayout(this);

    m_nameLine = new QLineEdit(this);
    m_nameLine->setPlaceholderText(i18n("Pretty name"));
    layout->addRow(i18nc("@label:textbox", "Pretty name:"), m_nameLine);

    m_commandLine = new QLineEdit(this);
    m_commandLine->setPlaceholderText(i18n("Example: user@example.com -p 2222"));
    layout->addRow(i18nc("@label:textbox", "SSH arguments:"), m_commandLine);

    m_descriptionLine = new QLineEdit(this);
    layout->addRow(i18nc("@label:textbox", "Notes:"), m_descriptionLine);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &ManualEntryDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &ManualEntryDialog::reject);
    connect(m_nameLine, &QLineEdit::returnPressed, this, &ManualEntryDialog::accept);
    connect(m_commandLine, &QLineEdit::returnPressed, this, &ManualEntryDialog::accept);
}

std::optional<SshHelper::ManualEntry> ManualEntryDialog::createEntry(QWidget *parent)
{
    ManualEntryDialog dialog(parent);
    if (dialog.exec() == QDialog::Accepted) {
        SshHelper::ManualEntry entry = dialog.currentEntry();
        entry.id = SshHelper::generateManualEntryId();
        return entry;
    }
    return std::nullopt;
}

void ManualEntryDialog::accept()
{
    const QString name = m_nameLine->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), i18n("Please enter a name."));
        m_nameLine->setFocus();
        return;
    }

    const QStringList arguments = SshHelper::stringToArguments(m_commandLine->text());
    if (arguments.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), i18n("Please provide SSH arguments (for example a host or user@host)."));
        m_commandLine->setFocus();
        return;
    }

    QDialog::accept();
}

SshHelper::ManualEntry ManualEntryDialog::currentEntry() const
{
    SshHelper::ManualEntry entry;
    entry.name = m_nameLine->text().trimmed();
    entry.arguments = SshHelper::stringToArguments(m_commandLine->text());
    entry.description = m_descriptionLine->text().trimmed();
    return entry;
}
