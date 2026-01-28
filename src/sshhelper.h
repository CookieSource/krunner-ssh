#pragma once

#include <KRunner/AbstractRunner>
#include <KRunner/QueryMatch>
#include <KRunner/RunnerContext>

#include <KPluginMetaData>
#include <KConfigWatcher>
#include <KSharedConfig>

#include "sshhelper_common.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QVariantList>

class KConfigWatcher;

class SshHelperRunner : public KRunner::AbstractRunner
{
    Q_OBJECT

public:
    SshHelperRunner(QObject *parent, const KPluginMetaData &metaData, const QVariantList &args);
    ~SshHelperRunner() override = default;

    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;

private Q_SLOTS:
    void scheduleReload();

private:
    struct SshTarget {
        QString id;
        QString defaultLabel;
        QString label;
        QString description;
        QStringList sshArguments;
        QString hostName;
        QString dnsName;
        QString userName;
        SshHelper::EntryOrigin origin;
        bool isManual = false;
    };

    void ensureHostsLoaded();
    void reloadHosts();
    static QString normalized(const QString &text);
    static double computeFuzzyScore(const QString &candidate, const QString &pattern);
    static QString hostFromArguments(const QStringList &arguments);
    static int hostArgumentIndex(const QStringList &arguments);
    static QStringList applyUserToArguments(const QStringList &arguments, const QString &userName);
    static QString normalizedHost(const QString &host);
    QString resolveDnsNameForHost(const QString &host);
    bool launchPreferredTerminal(const QStringList &arguments);

    QVector<SshTarget> m_targets;
    QSet<QString> m_seenIds;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    KSharedConfig::Ptr m_config;
    QHash<QString, QString> m_customLabels;
    QHash<QString, QString> m_customUsernames;
    QVector<SshHelper::ManualEntry> m_manualEntries;
    KConfigWatcher::Ptr m_configWatcher;
    QString m_preferredTerminalId = QStringLiteral("auto");
    QString m_customTerminalCommand;
    QHash<QString, QString> m_dnsCache;
    QSet<QString> m_dnsFailures;
    bool m_loaded = false;
};
