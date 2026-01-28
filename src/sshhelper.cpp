#include "sshhelper.h"

#include "sshdiscovery.h"
#include "sshhelper_common.h"

#include <KLocalizedString>
#include <KPluginFactory>
#include <KRunner/RunnerContext>
#include <KRunner/RunnerSyntax>

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <KConfigWatcher>
#include <KSharedConfig>

#include <algorithm>
#include <utility>

K_PLUGIN_CLASS_WITH_JSON(SshHelperRunner, "sshhelper.json")

Q_LOGGING_CATEGORY(LOG_SSHHELPER, "org.kde.runners.sshhelper")

namespace
{
bool launchWithCustomDescriptor(const QString &descriptor, const QStringList &sshArgs)
{
    if (descriptor.isEmpty()) {
        return false;
    }

    QStringList parts = QProcess::splitCommand(descriptor);
    if (parts.isEmpty()) {
        return false;
    }

    const QString program = parts.takeFirst();
    const QString executable = QStandardPaths::findExecutable(program);
    if (executable.isEmpty()) {
        return false;
    }

    QStringList arguments = parts;
    arguments << QStringLiteral("ssh");
    arguments += sshArgs;
    return QProcess::startDetached(executable, arguments);
}

bool launchWithDashE(const QString &program, const QStringList &sshArgs, const QStringList &extraArgs = {})
{
    const QString executable = QStandardPaths::findExecutable(program);
    if (executable.isEmpty()) {
        return false;
    }

    QStringList arguments = extraArgs;
    arguments << QStringLiteral("-e");
    arguments << QStringLiteral("ssh");
    arguments += sshArgs;
    return QProcess::startDetached(executable, arguments);
}

bool launchWithDoubleDash(const QString &program, const QStringList &sshArgs, const QStringList &extraArgs = {})
{
    const QString executable = QStandardPaths::findExecutable(program);
    if (executable.isEmpty()) {
        return false;
    }

    QStringList arguments = extraArgs;
    arguments << QStringLiteral("--");
    arguments << QStringLiteral("ssh");
    arguments += sshArgs;
    return QProcess::startDetached(executable, arguments);
}

double subsequenceScore(const QString &text, const QString &pattern)
{
    if (pattern.isEmpty() || text.isEmpty()) {
        return 0.0;
    }

    int firstIndex = -1;
    int lastIndex = -1;
    int previousIndex = -1;
    int bestBlock = 0;
    int currentBlock = 0;
    int matched = 0;

    for (const QChar &c : pattern) {
        const int foundIndex = text.indexOf(c, previousIndex + 1);
        if (foundIndex < 0) {
            return 0.0;
        }
        if (firstIndex == -1) {
            firstIndex = foundIndex;
        }
        lastIndex = foundIndex;
        if (foundIndex == previousIndex + 1) {
            ++currentBlock;
        } else {
            currentBlock = 1;
        }
        bestBlock = qMax(bestBlock, currentBlock);
        previousIndex = foundIndex;
        ++matched;
    }

    const int span = qMax(1, lastIndex - firstIndex + 1);
    const double coverage = static_cast<double>(matched) / static_cast<double>(pattern.size());
    const double density = static_cast<double>(matched) / static_cast<double>(span);
    const double continuity = static_cast<double>(bestBlock) / static_cast<double>(pattern.size());
    const double prefixBoost = firstIndex == 0 ? 0.15 : 0.0;

    const double weighted = (0.45 * coverage) + (0.35 * continuity) + (0.20 * density) + prefixBoost;
    return qBound(0.0, weighted, 1.0);
}
} // namespace

SshHelperRunner::SshHelperRunner(QObject *parent, const KPluginMetaData &metaData, const QVariantList &args)
    : KRunner::AbstractRunner(parent, metaData)
{
    setObjectName(QStringLiteral("SshHelperRunner"));

    Q_UNUSED(args);

    addSyntax(KRunner::RunnerSyntax(QStringLiteral("ssh :q"), i18n("Start an SSH session that matches :q.")));
    addSyntax(KRunner::RunnerSyntax(QStringLiteral("ssh"), i18n("List SSH sessions you have used before.")));

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &SshHelperRunner::scheduleReload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &SshHelperRunner::scheduleReload);

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(250);
    connect(&m_reloadTimer, &QTimer::timeout, this, &SshHelperRunner::reloadHosts);

    m_config = KSharedConfig::openConfig(QStringLiteral("krunner_sshhelperrc"));
    if (m_config) {
        m_configWatcher = KConfigWatcher::create(m_config);
        connect(m_configWatcher.data(), &KConfigWatcher::configChanged, this, &SshHelperRunner::scheduleReload);
    }
}

void SshHelperRunner::match(KRunner::RunnerContext &context)
{
    const QString query = context.query().trimmed();
    if (!query.startsWith(QStringLiteral("ssh"), Qt::CaseInsensitive)) {
        return;
    }

    ensureHostsLoaded();
    if (m_targets.isEmpty()) {
        return;
    }

    QString pattern = query.mid(3).trimmed();
    QString searchPattern = pattern;
    QString explicitUser;
    const QRegularExpression userPattern(QStringLiteral("^([^\\s@]+)@(.+)$"));
    const QRegularExpressionMatch userMatch = userPattern.match(pattern);
    if (userMatch.hasMatch()) {
        const QString hostPart = userMatch.captured(2).trimmed();
        if (!hostPart.isEmpty()) {
            explicitUser = userMatch.captured(1).trimmed();
            searchPattern = hostPart;
        }
    }
    const bool showAll = searchPattern.isEmpty();

    for (const SshTarget &target : std::as_const(m_targets)) {
        double relevance = 0.3;
        if (!showAll) {
            const double onLabel = computeFuzzyScore(target.label, searchPattern);
            const double onArguments = computeFuzzyScore(target.sshArguments.join(QLatin1Char(' ')), searchPattern);
            const double onDescription = computeFuzzyScore(target.description, searchPattern);
            const double onDefaultLabel = target.label == target.defaultLabel ? 0.0 : computeFuzzyScore(target.defaultLabel, searchPattern);
            const double onDnsName = computeFuzzyScore(target.dnsName, searchPattern);
            const double onUserName = computeFuzzyScore(target.userName, searchPattern);
            const double onUserHost = target.userName.isEmpty() ? 0.0 : computeFuzzyScore(QStringLiteral("%1@%2").arg(target.userName, target.hostName), pattern);
            relevance = std::max({onLabel, onArguments, onDescription, onDefaultLabel, onDnsName, onUserName, onUserHost});
            if (relevance <= 0.0) {
                continue;
            }
        }

        KRunner::QueryMatch match(this);
        match.setId(target.id);
        match.setIconName(QStringLiteral("utilities-terminal"));
        match.setText(target.label);
        if (target.dnsName.isEmpty()) {
            match.setSubtext(target.description);
        } else if (target.description.isEmpty()) {
            match.setSubtext(i18n("DNS: %1", target.dnsName));
        } else {
            match.setSubtext(i18n("%1 (DNS: %2)", target.description, target.dnsName));
        }
        match.setRelevance(qBound(0.0, showAll ? qMax(relevance, 0.33) : relevance, 1.0));
        if (showAll) {
            match.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::Moderate);
        }
        QStringList matchArguments = target.sshArguments;
        if (!explicitUser.isEmpty()) {
            matchArguments = applyUserToArguments(matchArguments, explicitUser);
        }
        match.setData(matchArguments);
        context.addMatch(match);
    }
}

void SshHelperRunner::run(const KRunner::RunnerContext &, const KRunner::QueryMatch &match)
{
    const QStringList arguments = match.data().toStringList();
    if (arguments.isEmpty()) {
        qCWarning(LOG_SSHHELPER) << "No ssh arguments were stored for match" << match.id();
        return;
    }

    if (launchPreferredTerminal(arguments)) {
        return;
    }

    const QString sshHelperEnv = qEnvironmentVariable("SSH_HELPER_TERMINAL");
    if (launchWithCustomDescriptor(sshHelperEnv, arguments)) {
        return;
    }

    const QString terminalEnv = qEnvironmentVariable("TERMINAL");
    if (launchWithCustomDescriptor(terminalEnv, arguments)) {
        return;
    }

    if (launchWithDashE(QStringLiteral("konsole"), arguments, {QStringLiteral("--noclose")})) {
        return;
    }

    if (launchWithDoubleDash(QStringLiteral("gnome-terminal"), arguments)) {
        return;
    }

    if (launchWithDoubleDash(QStringLiteral("kgx"), arguments)) {
        return;
    }

    if (launchWithDashE(QStringLiteral("x-terminal-emulator"), arguments)) {
        return;
    }

    const QStringList dashETerminals = {
        QStringLiteral("kitty"),
        QStringLiteral("alacritty"),
        QStringLiteral("tilix"),
        QStringLiteral("xfce4-terminal"),
        QStringLiteral("lxterminal"),
        QStringLiteral("qterminal"),
        QStringLiteral("terminator"),
        QStringLiteral("mate-terminal"),
        QStringLiteral("wezterm"),
        QStringLiteral("urxvt"),
        QStringLiteral("sakura")
    };

    for (const QString &terminal : dashETerminals) {
        if (launchWithDashE(terminal, arguments)) {
            return;
        }
    }

    if (launchWithDashE(QStringLiteral("xterm"), arguments, {QStringLiteral("-hold")})) {
        return;
    }

    if (!QProcess::startDetached(QStringLiteral("ssh"), arguments)) {
        qCWarning(LOG_SSHHELPER) << "Failed to start ssh client for" << match.id();
    }
}

void SshHelperRunner::scheduleReload()
{
    m_loaded = false;
    if (!m_reloadTimer.isActive()) {
        m_reloadTimer.start();
    }
}

void SshHelperRunner::ensureHostsLoaded()
{
    if (!m_loaded) {
        reloadHosts();
    }
}

QString SshHelperRunner::normalized(const QString &text)
{
    QString simplified = text.simplified();
    return simplified.toCaseFolded();
}

double SshHelperRunner::computeFuzzyScore(const QString &candidate, const QString &pattern)
{
    const QString candidateNorm = normalized(candidate);
    const QString patternNorm = normalized(pattern);

    if (candidateNorm.isEmpty() || patternNorm.isEmpty()) {
        return 0.0;
    }

    if (candidateNorm == patternNorm) {
        return 1.0;
    }

    if (candidateNorm.startsWith(patternNorm)) {
        const double proximity = static_cast<double>(patternNorm.size()) / static_cast<double>(candidateNorm.size());
        return qBound(0.0, 0.8 + (0.2 * proximity), 1.0);
    }

    if (candidateNorm.contains(patternNorm)) {
        const double proximity = static_cast<double>(patternNorm.size()) / static_cast<double>(candidateNorm.size());
        return qBound(0.0, 0.6 + (0.2 * proximity), 1.0);
    }

    const QStringList tokens = patternNorm.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return 0.0;
    }

    double total = 0.0;
    for (const QString &token : tokens) {
        total += subsequenceScore(candidateNorm, token);
    }
    return qBound(0.0, total / tokens.size(), 1.0);
}

QString SshHelperRunner::hostFromArguments(const QStringList &arguments)
{
    if (arguments.isEmpty()) {
        return {};
    }

    static const QSet<QChar> optionsWithValue = {
        QLatin1Char('b'),
        QLatin1Char('c'),
        QLatin1Char('D'),
        QLatin1Char('E'),
        QLatin1Char('F'),
        QLatin1Char('I'),
        QLatin1Char('i'),
        QLatin1Char('J'),
        QLatin1Char('L'),
        QLatin1Char('l'),
        QLatin1Char('m'),
        QLatin1Char('O'),
        QLatin1Char('o'),
        QLatin1Char('p'),
        QLatin1Char('Q'),
        QLatin1Char('R'),
        QLatin1Char('S'),
        QLatin1Char('W'),
        QLatin1Char('w'),
    };

    QString candidate;
    bool consumeNext = false;
    bool afterDoubleDash = false;

    for (int i = 0; i < arguments.size(); ++i) {
        const QString &arg = arguments.at(i);

        if (consumeNext) {
            consumeNext = false;
            continue;
        }

        if (!afterDoubleDash && arg == QLatin1String("--")) {
            afterDoubleDash = true;
            continue;
        }

        if (!afterDoubleDash && arg.startsWith(QLatin1Char('-'))) {
            if (arg.startsWith(QLatin1String("--"))) {
                if (!arg.contains(QLatin1Char('='))) {
                    consumeNext = true;
                }
                continue;
            }

            if (arg.size() == 2) {
                const QChar option = arg.at(1);
                if (optionsWithValue.contains(option)) {
                    consumeNext = true;
                }
                continue;
            }

            const QChar option = arg.at(1);
            if (optionsWithValue.contains(option)) {
                continue;
            }
            continue;
        }

        candidate = arg;
    }

    return candidate;
}

int SshHelperRunner::hostArgumentIndex(const QStringList &arguments)
{
    if (arguments.isEmpty()) {
        return -1;
    }

    static const QSet<QChar> optionsWithValue = {
        QLatin1Char('b'),
        QLatin1Char('c'),
        QLatin1Char('D'),
        QLatin1Char('E'),
        QLatin1Char('F'),
        QLatin1Char('I'),
        QLatin1Char('i'),
        QLatin1Char('J'),
        QLatin1Char('L'),
        QLatin1Char('l'),
        QLatin1Char('m'),
        QLatin1Char('O'),
        QLatin1Char('o'),
        QLatin1Char('p'),
        QLatin1Char('Q'),
        QLatin1Char('R'),
        QLatin1Char('S'),
        QLatin1Char('W'),
        QLatin1Char('w'),
    };

    bool consumeNext = false;
    bool afterDoubleDash = false;
    int candidateIndex = -1;

    for (int i = 0; i < arguments.size(); ++i) {
        const QString &arg = arguments.at(i);

        if (consumeNext) {
            consumeNext = false;
            continue;
        }

        if (!afterDoubleDash && arg == QLatin1String("--")) {
            afterDoubleDash = true;
            continue;
        }

        if (!afterDoubleDash && arg.startsWith(QLatin1Char('-'))) {
            if (arg.startsWith(QLatin1String("--"))) {
                if (!arg.contains(QLatin1Char('='))) {
                    consumeNext = true;
                }
                continue;
            }

            if (arg.size() == 2) {
                const QChar option = arg.at(1);
                if (optionsWithValue.contains(option)) {
                    consumeNext = true;
                }
                continue;
            }

            const QChar option = arg.at(1);
            if (optionsWithValue.contains(option)) {
                continue;
            }
            continue;
        }

        candidateIndex = i;
    }

    return candidateIndex;
}

QStringList SshHelperRunner::applyUserToArguments(const QStringList &arguments, const QString &userName)
{
    if (userName.trimmed().isEmpty() || arguments.isEmpty()) {
        return arguments;
    }

    QStringList updated = arguments;
    const int index = hostArgumentIndex(updated);
    if (index < 0 || index >= updated.size()) {
        return updated;
    }

    QString host = updated.at(index).trimmed();
    if (host.isEmpty()) {
        return updated;
    }

    const int atIndex = host.lastIndexOf(QLatin1Char('@'));
    if (atIndex > 0) {
        host = host.mid(atIndex + 1);
    }

    updated[index] = QStringLiteral("%1@%2").arg(userName.trimmed(), host);
    return updated;
}

QString SshHelperRunner::normalizedHost(const QString &host)
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

QString SshHelperRunner::resolveDnsNameForHost(const QString &host)
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

    if (m_dnsCache.contains(ipCandidate)) {
        return m_dnsCache.value(ipCandidate);
    }
    if (m_dnsFailures.contains(ipCandidate)) {
        return {};
    }

    const QHostInfo info = QHostInfo::fromName(ipCandidate);
    if (info.error() != QHostInfo::NoError) {
        m_dnsFailures.insert(ipCandidate);
        return {};
    }

    QString resolved = info.hostName().trimmed();
    if (resolved.endsWith(QLatin1Char('.'))) {
        resolved.chop(1);
    }

    if (resolved.isEmpty() || resolved == ipCandidate) {
        m_dnsFailures.insert(ipCandidate);
        return {};
    }

    QHostAddress resolvedAddress;
    if (resolvedAddress.setAddress(resolved)) {
        m_dnsFailures.insert(ipCandidate);
        return {};
    }

    m_dnsCache.insert(ipCandidate, resolved);
    return resolved;
}
void SshHelperRunner::reloadHosts()
{
    const QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (homePath.isEmpty()) {
        qCWarning(LOG_SSHHELPER) << "Could not resolve the user's home directory.";
        m_targets.clear();
        m_seenIds.clear();
        m_loaded = true;
        return;
    }

    const QString sshDirPath = QDir(homePath).filePath(QStringLiteral(".ssh"));
    const QString configPath = QDir(sshDirPath).filePath(QStringLiteral("config"));
    const QString knownHostsPath = QDir(sshDirPath).filePath(QStringLiteral("known_hosts"));

    if (!m_watcher.files().isEmpty()) {
        m_watcher.removePaths(m_watcher.files());
    }
    if (!m_watcher.directories().isEmpty()) {
        m_watcher.removePaths(m_watcher.directories());
    }

    if (QDir(sshDirPath).exists()) {
        m_watcher.addPath(sshDirPath);
    }
    if (QFile::exists(configPath)) {
        m_watcher.addPath(configPath);
    }
    if (QFile::exists(knownHostsPath)) {
        m_watcher.addPath(knownHostsPath);
    }

    const QString helperConfig = SshHelper::configFilePath();
    if (!helperConfig.isEmpty() && QFile::exists(helperConfig)) {
        m_watcher.addPath(helperConfig);
    }

    m_targets.clear();
    m_seenIds.clear();
    m_dnsFailures.clear();

    m_customLabels = SshHelper::loadCustomLabels();
    m_customUsernames = SshHelper::loadCustomUsernames();
    m_manualEntries = SshHelper::loadManualEntries();

    const QVector<SshHelper::DiscoveredHost> discovered = SshHelper::discoverHosts(configPath, knownHostsPath);
    m_targets.reserve(discovered.size() + m_manualEntries.size());

    for (const auto &host : discovered) {
        SshTarget entry;
        entry.id = host.id;
        entry.defaultLabel = host.alias;
        entry.label = host.alias;
        entry.description = host.description;
        entry.userName = host.userName.trimmed();
        const QString customUser = m_customUsernames.value(entry.id).trimmed();
        if (!customUser.isEmpty()) {
            entry.userName = customUser;
        }
        entry.sshArguments = host.arguments;
        if (!entry.userName.isEmpty()) {
            entry.sshArguments = applyUserToArguments(entry.sshArguments, entry.userName);
        }
        entry.hostName = host.hostName.isEmpty() ? host.alias : host.hostName;
        entry.origin = host.origin;
        m_targets.push_back(entry);
        m_seenIds.insert(entry.id);
    }

    for (SshTarget &target : m_targets) {
        const QString custom = m_customLabels.value(target.id).trimmed();
        if (!custom.isEmpty()) {
            target.label = custom;
        }
    }

    for (const SshHelper::ManualEntry &manual : std::as_const(m_manualEntries)) {
        if (manual.id.isEmpty() || manual.arguments.isEmpty()) {
            continue;
        }

        SshTarget entry;
        entry.id = manual.id;
        entry.defaultLabel = manual.name.isEmpty() ? manual.id : manual.name;
        entry.label = entry.defaultLabel;
        entry.description = manual.description.isEmpty() ? i18n("Manual entry") : manual.description;
        entry.sshArguments = manual.arguments;
        entry.hostName = hostFromArguments(entry.sshArguments);
        entry.userName.clear();
        entry.origin = SshHelper::EntryOrigin::Manual;
        entry.isManual = true;

        if (m_seenIds.contains(entry.id)) {
            for (SshTarget &existing : m_targets) {
                if (existing.id == entry.id) {
                    existing.defaultLabel = entry.defaultLabel;
                    existing.label = entry.label;
                    existing.description = entry.description;
                    existing.sshArguments = entry.sshArguments;
                    existing.hostName = entry.hostName;
                    existing.userName = entry.userName;
                    existing.origin = entry.origin;
                    existing.isManual = true;
                    break;
                }
            }
        } else {
            m_targets.push_back(std::move(entry));
            m_seenIds.insert(manual.id);
        }
    }

    for (SshTarget &target : m_targets) {
        if (target.hostName.isEmpty()) {
            target.hostName = hostFromArguments(target.sshArguments);
        }
        if (target.hostName.isEmpty()) {
            target.hostName = target.defaultLabel;
        }
        target.dnsName = resolveDnsNameForHost(target.hostName);
    }

    const SshHelper::TerminalPreference terminalPref = SshHelper::loadTerminalPreference();
    m_preferredTerminalId = terminalPref.id.isEmpty() ? QStringLiteral("auto") : terminalPref.id;
    m_customTerminalCommand = terminalPref.customCommand.trimmed();

    std::sort(m_targets.begin(), m_targets.end(), [](const SshTarget &lhs, const SshTarget &rhs) {
        return QString::localeAwareCompare(lhs.label, rhs.label) < 0;
    });

    m_loaded = true;
}

bool SshHelperRunner::launchPreferredTerminal(const QStringList &arguments)
{
    if (m_preferredTerminalId.isEmpty() || m_preferredTerminalId == QStringLiteral("auto")) {
        return false;
    }

    if (m_preferredTerminalId == QStringLiteral("custom")) {
        return launchWithCustomDescriptor(m_customTerminalCommand, arguments);
    }

    if (m_preferredTerminalId == QStringLiteral("konsole")) {
        return launchWithDashE(QStringLiteral("konsole"), arguments, {QStringLiteral("--noclose")});
    }
    if (m_preferredTerminalId == QStringLiteral("gnome-terminal")) {
        return launchWithDoubleDash(QStringLiteral("gnome-terminal"), arguments);
    }
    if (m_preferredTerminalId == QStringLiteral("kgx")) {
        return launchWithDoubleDash(QStringLiteral("kgx"), arguments);
    }
    if (m_preferredTerminalId == QStringLiteral("xterm")) {
        return launchWithDashE(QStringLiteral("xterm"), arguments, {QStringLiteral("-hold")});
    }
    if (m_preferredTerminalId == QStringLiteral("x-terminal-emulator")) {
        return launchWithDashE(QStringLiteral("x-terminal-emulator"), arguments);
    }

    static const QSet<QString> dashETerminals = {
        QStringLiteral("kitty"),
        QStringLiteral("alacritty"),
        QStringLiteral("tilix"),
        QStringLiteral("xfce4-terminal"),
        QStringLiteral("lxterminal"),
        QStringLiteral("qterminal"),
        QStringLiteral("terminator"),
        QStringLiteral("mate-terminal"),
        QStringLiteral("wezterm"),
        QStringLiteral("urxvt"),
        QStringLiteral("sakura")
    };

    if (dashETerminals.contains(m_preferredTerminalId)) {
        return launchWithDashE(m_preferredTerminalId, arguments);
    }

    return launchWithCustomDescriptor(m_preferredTerminalId, arguments);
}

#include "sshhelper.moc"
