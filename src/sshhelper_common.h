#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <QVector>

namespace SshHelper
{
enum class EntryOrigin {
    Config,
    KnownHosts,
    Manual
};

struct ManualEntry {
    QString id;
    QString name;
    QString description;
    QStringList arguments;
};

struct TerminalOption {
    QString id;
    QString displayName;
};

struct TerminalPreference {
    QString id;
    QString customCommand;
};

QString entryIdForArguments(const QStringList &arguments);
QString configFilePath();
QHash<QString, QString> loadCustomLabels();
void saveCustomLabels(const QHash<QString, QString> &labels);
QHash<QString, QString> loadCustomUsernames();
void saveCustomUsernames(const QHash<QString, QString> &usernames);
QVector<ManualEntry> loadManualEntries();
void saveManualEntries(const QVector<ManualEntry> &entries);
QString generateManualEntryId();
QString argumentsToString(const QStringList &arguments);
QStringList stringToArguments(const QString &command);
QString originDisplayLabel(EntryOrigin origin);
QVector<TerminalOption> availableTerminalOptions();
TerminalPreference loadTerminalPreference();
void saveTerminalPreference(const TerminalPreference &preference);
QString terminalDisplayNameForId(const QString &id);
} // namespace SshHelper
