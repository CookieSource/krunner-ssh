#include "sshdiscovery.h"

#include <KLocalizedString>

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QTextStream>

namespace
{
QString stripComment(const QString &line)
{
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == u'"') {
            inQuotes = !inQuotes;
        } else if (c == u'#' && !inQuotes) {
            return line.left(i);
        }
    }
    return line;
}

struct ConfigState {
    QStringList hosts;
    QString hostname;
    QString user;
};

void commitConfigState(const ConfigState &state, QVector<SshHelper::DiscoveredHost> &out, QSet<QString> &seenIds)
{
    if (state.hosts.isEmpty()) {
        return;
    }

    for (QString alias : state.hosts) {
        alias = alias.trimmed();
        if (alias.isEmpty() || alias.contains(QLatin1Char('*')) || alias.contains(QLatin1Char('?'))) {
            continue;
        }

        const QStringList arguments = {alias};
        const QString id = SshHelper::entryIdForArguments(arguments);
        if (seenIds.contains(id)) {
            continue;
        }

        SshHelper::DiscoveredHost entry;
        entry.id = id;
        entry.alias = alias;
        entry.arguments = arguments;
        entry.hostName = state.hostname.isEmpty() ? alias : state.hostname;
        entry.userName = state.user;
        entry.origin = SshHelper::EntryOrigin::Config;

        if (!state.user.isEmpty() && !state.hostname.isEmpty()) {
            entry.description = i18n("%1 in SSH config", QStringLiteral("%1@%2").arg(state.user, state.hostname));
        } else if (!state.hostname.isEmpty()) {
            entry.description = i18n("%1 in SSH config", state.hostname);
        } else {
            entry.description = i18n("SSH config entry");
        }

        out.push_back(std::move(entry));
        seenIds.insert(id);
    }
}

void parseConfigFile(const QString &path, QVector<SshHelper::DiscoveredHost> &out, QSet<QString> &seenIds)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    ConfigState state;

    while (!stream.atEnd()) {
        const QString rawLine = stream.readLine();
        const QString stripped = stripComment(rawLine).trimmed();
        if (stripped.isEmpty()) {
            continue;
        }

        const QStringList parts = stripped.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            continue;
        }

        const QString keyword = parts.constFirst().toLower();
        if (keyword == QLatin1String("host")) {
            commitConfigState(state, out, seenIds);
            state.hosts = parts.mid(1);
            state.hostname.clear();
            state.user.clear();
        } else if (keyword == QLatin1String("hostname")) {
            if (parts.size() > 1) {
                state.hostname = parts.at(1);
            }
        } else if (keyword == QLatin1String("user")) {
            if (parts.size() > 1) {
                state.user = parts.at(1);
            }
        }
    }

    commitConfigState(state, out, seenIds);
}

QString hostNameFromKnownHostsEntry(const QString &entry)
{
    QString candidate = entry.trimmed();
    if (candidate.startsWith(QLatin1Char('['))) {
        const int closeIndex = candidate.indexOf(QLatin1Char(']'));
        if (closeIndex > 1) {
            return candidate.mid(1, closeIndex - 1);
        }
    }
    return candidate;
}

void parseKnownHosts(const QString &path, QVector<SshHelper::DiscoveredHost> &out, QSet<QString> &seenIds)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    while (!stream.atEnd()) {
        const QString stripped = stripComment(stream.readLine()).trimmed();
        if (stripped.isEmpty()) {
            continue;
        }
        if (stripped.startsWith(QLatin1Char('|'))) {
            continue;
        }

        const int spaceIndex = stripped.indexOf(QLatin1Char(' '));
        const QString hostsField = spaceIndex > 0 ? stripped.left(spaceIndex) : stripped;
        const QStringList hosts = hostsField.split(QLatin1Char(','), Qt::SkipEmptyParts);

        for (const QString &host : hosts) {
            QString candidate = host.trimmed();
            if (candidate.isEmpty()) {
                continue;
            }
            QString userName;
            const int atIndex = candidate.lastIndexOf(QLatin1Char('@'));
            if (atIndex > 0) {
                userName = candidate.left(atIndex);
                candidate = candidate.mid(atIndex + 1);
                if (candidate.isEmpty()) {
                    continue;
                }
            }
            if (candidate.startsWith(QLatin1Char('[')) && candidate.endsWith(QLatin1Char(']'))) {
                candidate = candidate.mid(1, candidate.size() - 2);
            }

            const QStringList arguments = {candidate};
            const QString id = SshHelper::entryIdForArguments(arguments);
            if (seenIds.contains(id)) {
                continue;
            }

            SshHelper::DiscoveredHost entry;
            entry.id = id;
            entry.alias = candidate;
            entry.description = i18n("known_hosts entry");
            entry.arguments = arguments;
            entry.hostName = hostNameFromKnownHostsEntry(candidate);
            entry.userName = userName;
            entry.origin = SshHelper::EntryOrigin::KnownHosts;

            out.push_back(std::move(entry));
            seenIds.insert(id);
        }
    }
}
} // namespace

namespace SshHelper
{
QVector<DiscoveredHost> discoverHosts(const QString &configPath, const QString &knownHostsPath)
{
    QVector<DiscoveredHost> hosts;
    QSet<QString> seenIds;
    hosts.reserve(64);

    parseConfigFile(configPath, hosts, seenIds);
    parseKnownHosts(knownHostsPath, hosts, seenIds);

    return hosts;
}
} // namespace SshHelper
