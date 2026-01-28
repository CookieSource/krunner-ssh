#include "sshhelperkcm.h"

#include "entriesmodel.h"
#include "manualentrydialog.h"

#include "../sshdiscovery.h"
#include "../sshhelper_common.h"

#include <KLocalizedString>
#include <KPluginFactory>

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QHash>
#include <QHostAddress>
#include <QHostInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSet>
#include <QStandardPaths>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
QString normalizedHost(const QString &host)
{
    QString candidate = host.trimmed();
    if (candidate.isEmpty()) {
        return {};
    }

    const int atIndex = candidate.lastIndexOf(QLatin1Char('@'));
    if (atIndex >= 0) {
        candidate = candidate.mid(atIndex + 1);
    }

    if (candidate.startsWith(QLatin1Char('['))) {
        const int closeIndex = candidate.indexOf(QLatin1Char(']'));
        if (closeIndex > 1) {
            candidate = candidate.mid(1, closeIndex - 1);
        }
    }

    const int scopeIndex = candidate.indexOf(QLatin1Char('%'));
    if (scopeIndex > 0) {
        candidate = candidate.left(scopeIndex);
    }

    if (candidate.endsWith(QLatin1Char('.'))) {
        candidate.chop(1);
    }

    return candidate;
}

QString resolveDnsNameForHost(const QString &host, QHash<QString, QString> &cache, QSet<QString> &failures)
{
    const QString normalized = normalizedHost(host);
    if (normalized.isEmpty()) {
        return {};
    }

    QString ipCandidate = normalized;
    QHostAddress address;
    if (!address.setAddress(ipCandidate)) {
        if (ipCandidate.count(QLatin1Char(':')) == 1 && ipCandidate.contains(QLatin1Char('.'))) {
            const QString stripped = ipCandidate.section(QLatin1Char(':'), 0, 0);
            if (!address.setAddress(stripped)) {
                return {};
            }
            ipCandidate = stripped;
        } else {
            return {};
        }
    }

    if (cache.contains(ipCandidate)) {
        return cache.value(ipCandidate);
    }
    if (failures.contains(ipCandidate)) {
        return {};
    }

    const QHostInfo info = QHostInfo::fromName(ipCandidate);
    if (info.error() != QHostInfo::NoError) {
        failures.insert(ipCandidate);
        return {};
    }

    QString resolved = info.hostName().trimmed();
    if (resolved.endsWith(QLatin1Char('.'))) {
        resolved.chop(1);
    }

    if (resolved.isEmpty() || resolved == ipCandidate) {
        failures.insert(ipCandidate);
        return {};
    }

    QHostAddress resolvedAddress;
    if (resolvedAddress.setAddress(resolved)) {
        failures.insert(ipCandidate);
        return {};
    }

    cache.insert(ipCandidate, resolved);
    return resolved;
}
} // namespace

K_PLUGIN_CLASS(SshHelperConfigModule)

SshHelperConfigModule::SshHelperConfigModule(QObject *parent, const KPluginMetaData &metaData)
    : KCModule(parent, metaData)
{
    setupUi();
    connect(m_model, &EntriesModel::dirtyChanged, this, [this](bool dirty) {
        setNeedsSave(dirty);
    });
    connect(m_searchField, &QLineEdit::textChanged, m_model, &EntriesModel::setFilterString);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SshHelperConfigModule::updateButtons);
    connect(m_addButton, &QPushButton::clicked, this, [this]() {
        const auto entry = ManualEntryDialog::createEntry(widget());
        if (entry.has_value()) {
            m_model->addManualEntry(*entry);
            updateButtons();
        }
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        const QModelIndexList selection = m_tableView->selectionModel()->selectedRows();
        const QList<int> rows = uniqueRowsFromSelection(selection);
        if (m_model->removeManualRows(rows)) {
            updateButtons();
        }
    });
    connect(m_resetButton, &QPushButton::clicked, this, [this]() {
        const QModelIndexList selection = m_tableView->selectionModel()->selectedRows();
        const QList<int> rows = uniqueRowsFromSelection(selection);
        m_model->resetLabelsToDefault(rows);
        updateButtons();
    });
    connect(m_terminalCombo, &QComboBox::currentIndexChanged, this, [this]() {
        updateTerminalControls();
        setNeedsSave(true);
    });
    connect(m_terminalCustom, &QLineEdit::textChanged, this, [this]() {
        if (m_terminalCombo->currentData().toString() == QStringLiteral("custom")) {
            setNeedsSave(true);
        }
    });

    refreshModel();
    updateButtons();
    updateTerminalControls();
}

void SshHelperConfigModule::setupUi()
{
    auto *mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_searchField = new QLineEdit(widget());
    m_searchField->setPlaceholderText(i18n("Search entries"));
    mainLayout->addWidget(m_searchField);

    m_model = new EntriesModel(this);

    m_tableView = new QTableView(widget());
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->verticalHeader()->setVisible(false);
    mainLayout->addWidget(m_tableView, 1);

    auto *terminalRow = new QHBoxLayout;
    auto *terminalLabel = new QLabel(i18nc("@label:listbox", "Preferred terminal:"), widget());
    terminalRow->addWidget(terminalLabel);

    m_terminalCombo = new QComboBox(widget());
    terminalLabel->setBuddy(m_terminalCombo);
    terminalRow->addWidget(m_terminalCombo, 1);

    m_terminalCustom = new QLineEdit(widget());
    m_terminalCustom->setPlaceholderText(i18n("Command, for example: kitty -e"));
    terminalRow->addWidget(m_terminalCustom, 1);

    mainLayout->addLayout(terminalRow);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);

    m_addButton = new QPushButton(i18nc("@action:button", "Add"), widget());
    buttonRow->addWidget(m_addButton);

    m_removeButton = new QPushButton(i18nc("@action:button", "Remove"), widget());
    buttonRow->addWidget(m_removeButton);

    m_resetButton = new QPushButton(i18nc("@action:button", "Reset Name"), widget());
    buttonRow->addWidget(m_resetButton);

    mainLayout->addLayout(buttonRow);

    widget()->setLayout(mainLayout);
}

void SshHelperConfigModule::refreshModel()
{
    const QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString sshDirPath = homePath.isEmpty() ? QString() : QDir(homePath).filePath(QStringLiteral(".ssh"));
    const QString configPath = sshDirPath.isEmpty() ? QString() : QDir(sshDirPath).filePath(QStringLiteral("config"));
    const QString knownHostsPath = sshDirPath.isEmpty() ? QString() : QDir(sshDirPath).filePath(QStringLiteral("known_hosts"));

    const QVector<SshHelper::DiscoveredHost> discovered = SshHelper::discoverHosts(configPath, knownHostsPath);
    const QHash<QString, QString> customLabels = SshHelper::loadCustomLabels();
    const QHash<QString, QString> customUsernames = SshHelper::loadCustomUsernames();
    const QVector<SshHelper::ManualEntry> manualEntries = SshHelper::loadManualEntries();
    const QVector<SshHelper::TerminalOption> terminalOptions = SshHelper::availableTerminalOptions();
    const SshHelper::TerminalPreference terminalPreference = SshHelper::loadTerminalPreference();

    {
        QSignalBlocker blocker(m_terminalCombo);
        m_terminalCombo->clear();
        m_terminalCombo->addItem(i18n("Automatic (choose best available)"), QStringLiteral("auto"));
        for (const auto &option : terminalOptions) {
            m_terminalCombo->addItem(option.displayName, option.id);
        }
        m_terminalCombo->addItem(i18n("Custom command"), QStringLiteral("custom"));
        int index = m_terminalCombo->findData(terminalPreference.id);
        if (index < 0) {
            index = 0;
        }
        m_terminalCombo->setCurrentIndex(index);
    }

    {
        QSignalBlocker blocker(m_terminalCustom);
        m_terminalCustom->setText(terminalPreference.customCommand);
    }

    updateTerminalControls();

    QVector<EntriesModel::EntryRecord> records;
    QHash<QString, QString> dnsCache;
    QSet<QString> dnsFailures;
    records.reserve(discovered.size() + manualEntries.size());

    for (const auto &host : discovered) {
        EntriesModel::EntryRecord record;
        record.id = host.id;
        record.defaultLabel = host.alias;
        const QString custom = customLabels.value(host.id).trimmed();
        record.label = custom.isEmpty() ? host.alias : custom;
        record.initialLabel = record.label;
        record.description = host.description;
        record.initialDescription = host.description;
        record.defaultUserName = host.userName;
        const QString customUser = customUsernames.value(host.id).trimmed();
        record.userName = customUser.isEmpty() ? host.userName : customUser;
        record.initialUserName = record.userName;
        record.arguments = host.arguments;
        record.initialArguments = host.arguments;
        const QString hostName = host.hostName.isEmpty() ? host.alias : host.hostName;
        record.dnsName = resolveDnsNameForHost(hostName, dnsCache, dnsFailures);
        record.origin = host.origin;
        records.push_back(std::move(record));
    }

    for (const auto &manual : manualEntries) {
        EntriesModel::EntryRecord record;
        record.id = manual.id;
        record.defaultLabel = manual.name.isEmpty() ? manual.id : manual.name;
        record.label = record.defaultLabel;
        record.initialLabel = record.label;
        record.description = manual.description;
        record.initialDescription = manual.description;
        record.defaultUserName.clear();
        record.userName.clear();
        record.initialUserName.clear();
        record.arguments = manual.arguments;
        record.initialArguments = manual.arguments;
        record.dnsName.clear();
        record.origin = SshHelper::EntryOrigin::Manual;
        records.push_back(std::move(record));
    }

    std::sort(records.begin(), records.end(), [](const EntriesModel::EntryRecord &lhs, const EntriesModel::EntryRecord &rhs) {
        return QString::localeAwareCompare(lhs.label, rhs.label) < 0;
    });

    m_model->setEntries(std::move(records));
    m_model->markSaved();
    setNeedsSave(false);
}

void SshHelperConfigModule::updateButtons()
{
    if (!m_tableView->selectionModel()) {
        m_removeButton->setEnabled(false);
        m_resetButton->setEnabled(false);
        return;
    }

    const QModelIndexList selection = m_tableView->selectionModel()->selectedRows();
    bool hasManual = false;
    bool hasAutomatic = false;

    for (const QModelIndex &index : selection) {
        const auto origin = m_model->originAtRow(index.row());
        if (origin == SshHelper::EntryOrigin::Manual) {
            hasManual = true;
        } else {
            hasAutomatic = true;
        }
    }

    m_removeButton->setEnabled(hasManual);
    m_resetButton->setEnabled(hasAutomatic);
}

void SshHelperConfigModule::updateTerminalControls()
{
    if (!m_terminalCombo || !m_terminalCustom) {
        return;
    }

    const QString id = m_terminalCombo->currentData().toString();
    const bool isCustom = id == QStringLiteral("custom");
    m_terminalCustom->setEnabled(isCustom);
    m_terminalCustom->setVisible(true);
}

void SshHelperConfigModule::load()
{
    refreshModel();
    KCModule::load();
    updateButtons();
}

void SshHelperConfigModule::save()
{
    KCModule::save();

    const QVector<EntriesModel::EntryRecord> entries = m_model->entries();
    QHash<QString, QString> customLabels;
    QHash<QString, QString> customUsernames;
    QVector<SshHelper::ManualEntry> manualEntries;
    customLabels.reserve(entries.size());
    customUsernames.reserve(entries.size());
    manualEntries.reserve(entries.size());

    for (const auto &entry : entries) {
        if (entry.origin == SshHelper::EntryOrigin::Manual) {
            SshHelper::ManualEntry manual;
            manual.id = entry.id;
            manual.name = entry.label.trimmed();
            manual.description = entry.description.trimmed();
            manual.arguments = entry.arguments;
            if (!manual.id.isEmpty() && !manual.arguments.isEmpty()) {
                if (manual.name.isEmpty()) {
                    manual.name = SshHelper::argumentsToString(manual.arguments);
                }
                manualEntries.push_back(std::move(manual));
            }
        } else {
            const QString trimmed = entry.label.trimmed();
            if (!trimmed.isEmpty() && trimmed != entry.defaultLabel) {
                customLabels.insert(entry.id, trimmed);
            }
            const QString userTrimmed = entry.userName.trimmed();
            if (!userTrimmed.isEmpty() && userTrimmed != entry.defaultUserName) {
                customUsernames.insert(entry.id, userTrimmed);
            }
        }
    }

    SshHelper::saveCustomLabels(customLabels);
    SshHelper::saveCustomUsernames(customUsernames);
    SshHelper::saveManualEntries(manualEntries);

    SshHelper::TerminalPreference preference;
    preference.id = m_terminalCombo->currentData().toString();
    if (preference.id == QStringLiteral("custom")) {
        preference.customCommand = m_terminalCustom->text().trimmed();
    }
    SshHelper::saveTerminalPreference(preference);

    m_model->markSaved();
    setNeedsSave(false);
}

void SshHelperConfigModule::defaults()
{
    KCModule::defaults();
    m_model->resetToDefaults();
    if (m_terminalCombo) {
        m_terminalCombo->setCurrentIndex(0);
    }
    if (m_terminalCustom) {
        m_terminalCustom->clear();
    }
    updateTerminalControls();
    setNeedsSave(true);
    updateButtons();
}

#include "sshhelperkcm.moc"
