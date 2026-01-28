#pragma once

#include "../sshhelper_common.h"

#include <QDialog>
#include <optional>

class QLineEdit;

class ManualEntryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ManualEntryDialog(QWidget *parent = nullptr);

    static std::optional<SshHelper::ManualEntry> createEntry(QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    SshHelper::ManualEntry currentEntry() const;

    QLineEdit *m_nameLine = nullptr;
    QLineEdit *m_commandLine = nullptr;
    QLineEdit *m_descriptionLine = nullptr;
};
