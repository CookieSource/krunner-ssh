#include "entriesmodel.h"

#include <KLocalizedString>

#include <QSet>
#include <QStringList>
#include <algorithm>
#include <utility>

EntriesModel::EntriesModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int EntriesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_visibleRows.count();
}

int EntriesModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant EntriesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size()) {
        return {};
    }

    const EntryRecord &entry = entryForVisibleRow(index.row());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case PrettyNameColumn:
            return entry.label;
        case CommandColumn:
            return SshHelper::argumentsToString(entry.arguments);
        case UserColumn:
            return entry.userName;
        case DnsColumn:
            return entry.dnsName;
        case SourceColumn:
            return SshHelper::originDisplayLabel(entry.origin);
        case NotesColumn:
            return entry.description;
        default:
            break;
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
        case PrettyNameColumn:
            if (!entry.defaultLabel.isEmpty() && entry.label != entry.defaultLabel) {
                return i18n("Custom label for %1", entry.defaultLabel);
            }
            return entry.defaultLabel;
        case CommandColumn:
            return SshHelper::argumentsToString(entry.arguments);
        case UserColumn:
            return entry.userName;
        case DnsColumn:
            return entry.dnsName;
        case NotesColumn:
            return entry.description;
        case SourceColumn:
            return SshHelper::originDisplayLabel(entry.origin);
        default:
            break;
        }
    }

    return {};
}

QVariant EntriesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section) {
    case PrettyNameColumn:
        return i18n("Pretty Name");
    case CommandColumn:
        return i18n("SSH Target");
    case UserColumn:
        return i18n("User");
    case DnsColumn:
        return i18n("DNS Name");
    case SourceColumn:
        return i18n("Source");
    case NotesColumn:
        return i18n("Notes");
    default:
        break;
    }
    return {};
}

Qt::ItemFlags EntriesModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    Qt::ItemFlags base = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    const EntryRecord &entry = entryForVisibleRow(index.row());

    if (index.column() == PrettyNameColumn) {
        base |= Qt::ItemIsEditable;
    } else if (index.column() == UserColumn) {
        if (!entry.isManual()) {
            base |= Qt::ItemIsEditable;
        }
    } else if (index.column() == CommandColumn || index.column() == NotesColumn) {
        if (entry.isManual()) {
            base |= Qt::ItemIsEditable;
        }
    }
    return base;
}

bool EntriesModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }

    EntryRecord &entry = entryForVisibleRow(index.row());
    const QString textValue = value.toString();
    bool changed = false;

    switch (index.column()) {
    case PrettyNameColumn: {
        const QString trimmed = textValue.trimmed();
        if (entry.label != trimmed) {
            entry.label = trimmed;
            changed = true;
        }
        break;
    }
    case UserColumn: {
        if (entry.isManual()) {
            return false;
        }
        const QString trimmed = textValue.trimmed();
        if (entry.userName != trimmed) {
            entry.userName = trimmed;
            changed = true;
        }
        break;
    }
    case CommandColumn: {
        if (!entry.isManual()) {
            return false;
        }
        const QStringList arguments = SshHelper::stringToArguments(textValue);
        if (entry.arguments != arguments) {
            entry.arguments = arguments;
            changed = true;
        }
        break;
    }
    case NotesColumn: {
        if (!entry.isManual()) {
            return false;
        }
        const QString trimmed = textValue.trimmed();
        if (entry.description != trimmed) {
            entry.description = trimmed;
            changed = true;
        }
        break;
    }
    default:
        return false;
    }

    if (!changed) {
        return false;
    }

    Q_EMIT dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole});
    updateDirtyState();
    return true;
}

void EntriesModel::setEntries(QVector<EntryRecord> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    m_filter.clear();
    m_initialManualIds.clear();
    for (const EntryRecord &entry : std::as_const(m_entries)) {
        if (entry.isManual()) {
            m_initialManualIds.append(entry.id);
        }
    }
    std::sort(m_initialManualIds.begin(), m_initialManualIds.end());
    rebuildVisibleRows();
    endResetModel();
    updateDirtyState();
}

QVector<EntriesModel::EntryRecord> EntriesModel::entries() const
{
    return m_entries;
}

void EntriesModel::setFilterString(const QString &text)
{
    const QString normalized = text.simplified().toCaseFolded();
    if (m_filter == normalized) {
        return;
    }
    m_filter = normalized;
    beginResetModel();
    rebuildVisibleRows();
    endResetModel();
}

bool EntriesModel::removeManualRows(const QList<int> &rows)
{
    if (rows.isEmpty()) {
        return false;
    }

    QSet<int> removalIndices;
    for (int row : rows) {
        if (row < 0 || row >= m_visibleRows.size()) {
            continue;
        }
        const int entryIndex = m_visibleRows.at(row);
        if (m_entries.at(entryIndex).isManual()) {
            removalIndices.insert(entryIndex);
        }
    }

    if (removalIndices.isEmpty()) {
        return false;
    }

    beginResetModel();
    QVector<EntryRecord> kept;
    kept.reserve(m_entries.size() - removalIndices.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        if (!removalIndices.contains(i)) {
            kept.append(m_entries.at(i));
        }
    }
    m_entries = std::move(kept);
    rebuildVisibleRows();
    endResetModel();

    updateDirtyState();
    return true;
}

void EntriesModel::resetLabelsToDefault(const QList<int> &rows)
{
    bool anyChanged = false;
    for (int row : rows) {
        if (row < 0 || row >= m_visibleRows.size()) {
            continue;
        }
        EntryRecord &entry = entryForVisibleRow(row);
        if (entry.isManual()) {
            continue;
        }
        if (entry.label != entry.defaultLabel) {
            entry.label = entry.defaultLabel;
            anyChanged = true;
        }
    }
    if (!anyChanged) {
        return;
    }

    const bool needsRefilter = !m_filter.isEmpty();
    if (needsRefilter) {
        beginResetModel();
        rebuildVisibleRows();
        endResetModel();
    } else {
        for (int row : rows) {
            if (row < 0 || row >= m_visibleRows.size()) {
                continue;
            }
            const QModelIndex idx = index(row, PrettyNameColumn);
            Q_EMIT dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole});
        }
    }
    updateDirtyState();
}

void EntriesModel::addManualEntry(const SshHelper::ManualEntry &entry)
{
    EntryRecord record;
    record.id = entry.id;
    record.defaultLabel = entry.name.isEmpty() ? entry.id : entry.name;
    record.label = record.defaultLabel;
    record.initialLabel = record.label;
    record.description = entry.description;
    record.initialDescription = entry.description;
    record.defaultUserName.clear();
    record.userName.clear();
    record.initialUserName.clear();
    record.arguments = entry.arguments;
    record.initialArguments = entry.arguments;
    record.dnsName.clear();
    record.origin = SshHelper::EntryOrigin::Manual;

    beginResetModel();
    m_entries.append(record);
    rebuildVisibleRows();
    endResetModel();

    updateDirtyState();
}

void EntriesModel::resetToDefaults()
{
    beginResetModel();
    QVector<EntryRecord> kept;
    kept.reserve(m_entries.size());
    for (EntryRecord entry : std::as_const(m_entries)) {
        if (entry.isManual()) {
            continue;
        }
        if (entry.label != entry.defaultLabel) {
            entry.label = entry.defaultLabel;
        }
        if (entry.userName != entry.defaultUserName) {
            entry.userName = entry.defaultUserName;
        }
        kept.append(std::move(entry));
    }
    m_entries = std::move(kept);
    rebuildVisibleRows();
    endResetModel();

    updateDirtyState();
}

void EntriesModel::markSaved()
{
    m_initialManualIds.clear();
    for (EntryRecord &entry : m_entries) {
        entry.initialLabel = entry.label;
        entry.initialUserName = entry.userName;
        entry.initialArguments = entry.arguments;
        entry.initialDescription = entry.description;
        if (entry.isManual()) {
            m_initialManualIds.append(entry.id);
        }
    }
    std::sort(m_initialManualIds.begin(), m_initialManualIds.end());
    updateDirtyState();
}

bool EntriesModel::isDirty() const
{
    return m_dirty;
}

SshHelper::EntryOrigin EntriesModel::originAtRow(int row) const
{
    if (row < 0 || row >= m_visibleRows.size()) {
        return SshHelper::EntryOrigin::Config;
    }
    return entryForVisibleRow(row).origin;
}

EntriesModel::EntryRecord &EntriesModel::entryForVisibleRow(int row)
{
    const int index = m_visibleRows.at(row);
    return m_entries[index];
}

const EntriesModel::EntryRecord &EntriesModel::entryForVisibleRow(int row) const
{
    const int index = m_visibleRows.at(row);
    return m_entries.at(index);
}

void EntriesModel::rebuildVisibleRows()
{
    m_visibleRows.clear();
    m_visibleRows.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        if (matchesFilter(m_entries.at(i))) {
            m_visibleRows.append(i);
        }
    }
}

bool EntriesModel::matchesFilter(const EntryRecord &entry) const
{
    if (m_filter.isEmpty()) {
        return true;
    }

    const QStringList parts = {
        entry.label,
        entry.defaultLabel,
        entry.userName,
        entry.dnsName,
        SshHelper::argumentsToString(entry.arguments),
        entry.description,
    };
    const QString haystack = parts.join(QLatin1Char(' ')).toCaseFolded();
    return haystack.contains(m_filter);
}

void EntriesModel::updateDirtyState()
{
    bool dirty = false;
    QStringList manualIds;
    manualIds.reserve(m_entries.size());

    for (const EntryRecord &entry : std::as_const(m_entries)) {
        if (entry.isManual()) {
            manualIds.append(entry.id);
            if (!dirty && (entry.label != entry.initialLabel || entry.arguments != entry.initialArguments
                           || entry.description != entry.initialDescription || entry.userName != entry.initialUserName)) {
                dirty = true;
            }
        } else {
            if (!dirty && (entry.label != entry.initialLabel || entry.userName != entry.initialUserName)) {
                dirty = true;
            }
        }
    }

    std::sort(manualIds.begin(), manualIds.end());
    if (!dirty && manualIds != m_initialManualIds) {
        dirty = true;
    }

    if (dirty != m_dirty) {
        m_dirty = dirty;
        Q_EMIT dirtyChanged(m_dirty);
    }
}

QList<int> uniqueRowsFromSelection(const QModelIndexList &indexes)
{
    QSet<int> rows;
    for (const QModelIndex &index : indexes) {
        if (index.isValid()) {
            rows.insert(index.row());
        }
    }
    QList<int> result = rows.values();
    std::sort(result.begin(), result.end());
    return result;
}
