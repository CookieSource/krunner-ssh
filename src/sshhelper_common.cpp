#include "sshhelper_common.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCryptographicHash>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#include <iterator>

namespace
{
constexpr auto s_configFileName = "krunner_sshhelperrc";
constexpr auto s_aliasGroup = "Aliases";
constexpr auto s_usernameGroup = "Usernames";
constexpr auto s_manualGroup = "ManualEntries";
constexpr auto s_idsKey = "Ids";
constexpr auto s_nameKey = "Name";
constexpr auto s_argumentsKey = "Arguments";
constexpr auto s_descriptionKey = "Description";
constexpr auto s_terminalGroup = "Terminal";
constexpr auto s_terminalIdKey = "Id";
constexpr auto s_terminalCustomKey = "CustomCommand";

struct TerminalCandidate {
    const char *id;
    const char *displayName;
    const char *executable;
};

const TerminalCandidate TERMINAL_CANDIDATES[] = {
    {"konsole", "Konsole", "konsole"},
    {"gnome-terminal", "GNOME Terminal", "gnome-terminal"},
    {"kgx", "GNOME Console (kgx)", "kgx"},
    {"kitty", "Kitty", "kitty"},
    {"alacritty", "Alacritty", "alacritty"},
    {"tilix", "Tilix", "tilix"},
    {"xfce4-terminal", "Xfce4 Terminal", "xfce4-terminal"},
    {"lxterminal", "LXTerminal", "lxterminal"},
    {"qterminal", "QTerminal", "qterminal"},
    {"terminator", "Terminator", "terminator"},
    {"mate-terminal", "MATE Terminal", "mate-terminal"},
    {"wezterm", "WezTerm", "wezterm"},
    {"urxvt", "rxvt-unicode", "urxvt"},
    {"sakura", "Sakura", "sakura"},
    {"xterm", "xterm", "xterm"},
    {"x-terminal-emulator", "System Default (x-terminal-emulator)", "x-terminal-emulator"},
};

QStringList normalizedArguments(const QStringList &arguments)
{
    QStringList normalized;
    normalized.reserve(arguments.size());
    for (const QString &arg : arguments) {
        normalized << arg.trimmed();
    }
    return normalized;
}

KSharedConfig::Ptr openConfig()
{
    return KSharedConfig::openConfig(QString::fromLatin1(s_configFileName));
}
} // namespace

namespace SshHelper
{
QString entryIdForArguments(const QStringList &arguments)
{
    const QStringList normalized = normalizedArguments(arguments);
    QByteArray buffer;
    buffer.reserve(128);
    for (const QString &arg : normalized) {
        const QByteArray utf8 = arg.toUtf8();
        buffer.append(utf8);
        buffer.append('\x1f'); // unit separator
    }
    const QByteArray hash = QCryptographicHash::hash(buffer, QCryptographicHash::Sha1);
    return QStringLiteral("auto:%1").arg(QString::fromLatin1(hash.toHex()));
}

QString configFilePath()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configDir.isEmpty()) {
        return {};
    }
    return QDir(configDir).filePath(QString::fromLatin1(s_configFileName));
}

QHash<QString, QString> loadCustomLabels()
{
    QHash<QString, QString> result;
    const KSharedConfig::Ptr cfg = openConfig();
    if (!cfg) {
        return result;
    }

    const KConfigGroup group(cfg, QString::fromLatin1(s_aliasGroup));
    const auto keys = group.keyList();
    result.reserve(keys.size());
    for (const QString &key : keys) {
        const QString value = group.readEntry(key, QString()).trimmed();
        if (!value.isEmpty()) {
            result.insert(key, value);
        }
    }
    return result;
}

void saveCustomLabels(const QHash<QString, QString> &labels)
{
    const KSharedConfig::Ptr cfg = openConfig();
    if (!cfg) {
        return;
    }

    KConfigGroup group(cfg, QString::fromLatin1(s_aliasGroup));
    group.deleteGroup();
    for (auto it = labels.cbegin(); it != labels.cend(); ++it) {
        group.writeEntry(it.key(), it.value());
    }
    cfg->sync();
}

QHash<QString, QString> loadCustomUsernames()
{
    QHash<QString, QString> result;
    const KSharedConfig::Ptr cfg = openConfig();
    if (!cfg) {
        return result;
    }

    const KConfigGroup group(cfg, QString::fromLatin1(s_usernameGroup));
    const auto keys = group.keyList();
    result.reserve(keys.size());
    for (const QString &key : keys) {
        const QString value = group.readEntry(key, QString()).trimmed();
        if (!value.isEmpty()) {
            result.insert(key, value);
        }
    }
    return result;
}

void saveCustomUsernames(const QHash<QString, QString> &usernames)
{
    const KSharedConfig::Ptr cfg = openConfig();
    if (!cfg) {
        return;
    }

    KConfigGroup group(cfg, QString::fromLatin1(s_usernameGroup));
    group.deleteGroup();
    for (auto it = usernames.cbegin(); it != usernames.cend(); ++it) {
        const QString value = it.value().trimmed();
        if (!value.isEmpty()) {
            group.writeEntry(it.key(), value);
        }
    }
    cfg->sync();
}

QVector<ManualEntry> loadManualEntries()
{
    QVector<ManualEntry> entries;
    const KSharedConfig::Ptr cfg = openConfig();
    if (!cfg) {
        return entries;
    }

    const KConfigGroup group(cfg, QString::fromLatin1(s_manualGroup));
    const QStringList ids = group.readEntry(QString::fromLatin1(s_idsKey), QStringList());
    entries.reserve(ids.size());

    for (const QString &id : ids) {
        const KConfigGroup entryGroup(&group, id);
        ManualEntry entry;
        entry.id = id;
        entry.name = entryGroup.readEntry(QString::fromLatin1(s_nameKey), QString());
        entry.arguments = normalizedArguments(entryGroup.readEntry(QString::fromLatin1(s_argumentsKey), QStringList()));
        entry.description = entryGroup.readEntry(QString::fromLatin1(s_descriptionKey), QString());
        if (!entry.name.trimmed().isEmpty() && !entry.arguments.isEmpty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

void saveManualEntries(const QVector<ManualEntry> &entries)
{
    const KSharedConfig::Ptr cfg = openConfig();
    if (!cfg) {
        return;
    }

    KConfigGroup group(cfg, QString::fromLatin1(s_manualGroup));
    group.deleteGroup();

    QStringList ids;
    ids.reserve(entries.size());
    for (const ManualEntry &entry : entries) {
        if (entry.id.isEmpty()) {
            continue;
        }
        ids.push_back(entry.id);
        KConfigGroup entryGroup(&group, entry.id);
        entryGroup.writeEntry(QString::fromLatin1(s_nameKey), entry.name.trimmed());
        entryGroup.writeEntry(QString::fromLatin1(s_argumentsKey), normalizedArguments(entry.arguments));
        entryGroup.writeEntry(QString::fromLatin1(s_descriptionKey), entry.description.trimmed());
    }

    group.writeEntry(QString::fromLatin1(s_idsKey), ids);
    cfg->sync();
}

QString generateManualEntryId()
{
    return QStringLiteral("manual:%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString argumentsToString(const QStringList &arguments)
{
    return arguments.join(QLatin1Char(' '));
}

QStringList stringToArguments(const QString &command)
{
    return normalizedArguments(QProcess::splitCommand(command));
}

QString originDisplayLabel(EntryOrigin origin)
{
    switch (origin) {
    case EntryOrigin::Config:
        return i18n("SSH config");
    case EntryOrigin::KnownHosts:
        return i18n("Known hosts");
    case EntryOrigin::Manual:
        return i18n("Manual entry");
    }
    return {};
}

QVector<TerminalOption> availableTerminalOptions()
{
    QVector<TerminalOption> options;
    options.reserve(std::size(TERMINAL_CANDIDATES));
    for (const TerminalCandidate &candidate : TERMINAL_CANDIDATES) {
        if (!QStandardPaths::findExecutable(QString::fromLatin1(candidate.executable)).isEmpty()) {
            TerminalOption option;
            option.id = QString::fromLatin1(candidate.id);
            option.displayName = QString::fromLatin1(candidate.displayName);
            options.push_back(std::move(option));
        }
    }
    return options;
}

TerminalPreference loadTerminalPreference()
{
    TerminalPreference preference;
    preference.id = QStringLiteral("auto");

    const KSharedConfig::Ptr cfg = openConfig();
    if (!cfg) {
        return preference;
    }

    const KConfigGroup group(cfg, QString::fromLatin1(s_terminalGroup));
    preference.id = group.readEntry(QString::fromLatin1(s_terminalIdKey), QStringLiteral("auto"));
    preference.customCommand = group.readEntry(QString::fromLatin1(s_terminalCustomKey), QString());
    return preference;
}

void saveTerminalPreference(const TerminalPreference &preference)
{
    const KSharedConfig::Ptr cfg = openConfig();
    if (!cfg) {
        return;
    }

    KConfigGroup group(cfg, QString::fromLatin1(s_terminalGroup));
    if (preference.id.isEmpty() || preference.id == QLatin1String("auto")) {
        group.deleteGroup();
    } else {
        group.writeEntry(QString::fromLatin1(s_terminalIdKey), preference.id);
        if (preference.id == QLatin1String("custom")) {
            group.writeEntry(QString::fromLatin1(s_terminalCustomKey), preference.customCommand.trimmed());
        } else {
            group.deleteEntry(QString::fromLatin1(s_terminalCustomKey));
        }
    }
    cfg->sync();
}

QString terminalDisplayNameForId(const QString &id)
{
    if (id == QLatin1String("auto")) {
        return i18n("Automatic");
    }
    if (id == QLatin1String("custom")) {
        return i18n("Custom command");
    }
    for (const TerminalCandidate &candidate : TERMINAL_CANDIDATES) {
        if (id == QLatin1String(candidate.id)) {
            return QString::fromLatin1(candidate.displayName);
        }
    }
    return id;
}
} // namespace SshHelper
