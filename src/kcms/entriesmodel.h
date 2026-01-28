#pragma once

#include "sshhelper_common.h"

#include <QAbstractTableModel>
#include <QVector>

class EntriesModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        PrettyNameColumn = 0,
        CommandColumn,
        UserColumn,
        DnsColumn,
        SourceColumn,
        NotesColumn,
        ColumnCount
    };

    struct EntryRecord {
        QString id;
        QString defaultLabel;
        QString initialLabel;
        QString label;
        QString description;
        QString initialDescription;
        QString defaultUserName;
        QString userName;
        QString initialUserName;
        QStringList arguments;
        QStringList initialArguments;
        QString dnsName;
        SshHelper::EntryOrigin origin = SshHelper::EntryOrigin::Config;
        bool isManual() const { return origin == SshHelper::EntryOrigin::Manual; }
    };

    explicit EntriesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    void setEntries(QVector<EntryRecord> entries);
    QVector<EntryRecord> entries() const;

    void setFilterString(const QString &text);

    bool removeManualRows(const QList<int> &rows);
    void resetLabelsToDefault(const QList<int> &rows);
    void addManualEntry(const SshHelper::ManualEntry &entry);
    void resetToDefaults();
    void markSaved();

    bool isDirty() const;
    SshHelper::EntryOrigin originAtRow(int row) const;

Q_SIGNALS:
    void dirtyChanged(bool dirty);

private:
    EntryRecord &entryForVisibleRow(int row);
    const EntryRecord &entryForVisibleRow(int row) const;
    void rebuildVisibleRows();
    bool matchesFilter(const EntryRecord &entry) const;
    void updateDirtyState();

    QVector<EntryRecord> m_entries;
    QVector<int> m_visibleRows;
    QString m_filter;
    bool m_dirty = false;
    QStringList m_initialManualIds;
};

QList<int> uniqueRowsFromSelection(const QModelIndexList &indexes);
