#pragma once

#include <KCModule>

class EntriesModel;
class QLineEdit;
class QPushButton;
class QTableView;
class QComboBox;

class SshHelperConfigModule : public KCModule
{
    Q_OBJECT

public:
    explicit SshHelperConfigModule(QObject *parent, const KPluginMetaData &metaData);

    void load() override;
    void save() override;
    void defaults() override;

private:
    void setupUi();
    void refreshModel();
    void updateButtons();
    void updateTerminalControls();

    EntriesModel *m_model = nullptr;
    QLineEdit *m_searchField = nullptr;
    QTableView *m_tableView = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QComboBox *m_terminalCombo = nullptr;
    QLineEdit *m_terminalCustom = nullptr;
};
