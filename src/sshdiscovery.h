#pragma once

#include "sshhelper_common.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace SshHelper
{
struct DiscoveredHost {
    QString id;
    QString alias;
    QString description;
    QStringList arguments;
    QString hostName;
    QString userName;
    EntryOrigin origin = EntryOrigin::Config;
};

QVector<DiscoveredHost> discoverHosts(const QString &configPath, const QString &knownHostsPath);
}
