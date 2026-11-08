// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "historystore.h"

#include "mapping.h"

#include <QAtomicInt>
#include <QCoreApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {

const QLatin1String RingAccountPattern("/org/freedesktop/Telepathy/Account/ring/tel/%");

QString tr(const char *text)
{
    return QCoreApplication::translate("HistoryStore", text);
}

QString typeList(const QList<int> &types)
{
    QStringList list;
    for (int type : types)
        list.append(QString::number(type));
    return list.join(QLatin1Char(','));
}

bool exec(QSqlQuery &query, const QString &sql, QString *error)
{
    query.setForwardOnly(true);
    if (!query.exec(sql)) {
        if (error)
            *error = QCoreApplication::translate("HistoryStore", "Reading the message database failed: %1").arg(query.lastError().text());
        return false;
    }
    return true;
}

} // namespace

HistoryStore::HistoryStore(const QString &path)
    : m_path(path.isEmpty() ? defaultPath() : path)
{
    static QAtomicInt counter;
    m_connection = QStringLiteral("smscallbackup-%1").arg(counter.fetchAndAddRelaxed(1));
}

HistoryStore::~HistoryStore()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        if (db.isValid())
            db.close();
    }
    if (QSqlDatabase::contains(m_connection))
        QSqlDatabase::removeDatabase(m_connection);
}

QString HistoryStore::defaultPath()
{
    // The same location libcommhistory uses (CommHistoryDatabasePath).
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/commhistory/commhistory.db");
}

bool HistoryStore::exists()
{
    return QFile::exists(defaultPath());
}

bool HistoryStore::open(QString *error)
{
    if (!QFile::exists(m_path)) {
        *error = tr("There is no message database at %1.").arg(m_path);
        return false;
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(m_path);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=10000"));
    if (!db.open()) {
        *error = tr("Cannot open the message database: %1").arg(db.lastError().text());
        return false;
    }
    QSqlQuery query(db);
    if (!exec(query, QStringLiteral("SELECT id, type, startTime, endTime, direction, isRead, "
                                    "isMissedCall, status, localUid, remoteUid, freeText, groupId "
                                    "FROM Events LIMIT 0"), error)) {
        *error = tr("The message database has an unexpected layout: %1").arg(query.lastError().text());
        return false;
    }
    return true;
}

bool HistoryStore::counts(Counts *counts, QString *error)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (!exec(query, QStringLiteral("SELECT type, COUNT(*) FROM Events WHERE type IN (2, 3, 6) "
                                    "AND (isDeleted IS NULL OR isDeleted = 0) GROUP BY type"), error))
        return false;
    *counts = Counts();
    while (query.next()) {
        const int count = query.value(1).toInt();
        switch (query.value(0).toInt()) {
        case HistoryEvent::SmsEvent: counts->sms = count; break;
        case HistoryEvent::MmsEvent: counts->mms = count; break;
        case HistoryEvent::CallEvent: counts->calls = count; break;
        }
    }
    return true;
}

bool HistoryStore::readEvents(const QList<int> &types, const std::function<bool(const HistoryEvent &)> &callback,
                              QString *error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    // One read transaction: a consistent snapshot even while new messages arrive.
    db.transaction();

    QHash<int, QString> incomingStatus;
    QHash<int, QString> subscriberIdentity;
    {
        QSqlQuery query(db);
        if (!exec(query, QStringLiteral("SELECT eventId, key, value FROM EventProperties "
                                        "WHERE key IN ('incomingStatus', 'subscriberIdentity')"), error)) {
            db.rollback();
            return false;
        }
        while (query.next()) {
            if (query.value(1).toString() == QLatin1String("incomingStatus"))
                incomingStatus.insert(query.value(0).toInt(), query.value(2).toString());
            else
                subscriberIdentity.insert(query.value(0).toInt(), query.value(2).toString());
        }
    }

    QSqlQuery query(db);
    const QString sql = QStringLiteral(
            "SELECT id, type, startTime, endTime, direction, isDraft, isRead, isMissedCall, status, "
            "localUid, remoteUid, subject, freeText FROM Events "
            "WHERE type IN (%1) AND (isDeleted IS NULL OR isDeleted = 0) "
            "ORDER BY (CASE type WHEN 3 THEN startTime ELSE endTime END), id").arg(typeList(types));
    if (!exec(query, sql, error)) {
        db.rollback();
        return false;
    }
    while (query.next()) {
        HistoryEvent e;
        e.id = query.value(0).toInt();
        e.type = query.value(1).toInt();
        e.startTime = query.value(2).toLongLong();
        e.endTime = query.value(3).toLongLong();
        e.direction = query.value(4).toInt();
        e.isDraft = query.value(5).toInt() != 0;
        e.isRead = query.value(6).toInt() != 0;
        e.isMissedCall = query.value(7).toInt() != 0;
        e.status = query.value(8).toInt();
        e.localUid = query.value(9).toString();
        e.remoteUid = query.value(10).toString();
        e.subject = query.value(11).toString();
        e.freeText = query.value(12).toString();
        e.incomingStatus = incomingStatus.value(e.id);
        e.subscriberIdentity = subscriberIdentity.value(e.id);
        if (!callback(e))
            break;
    }
    db.rollback();
    return true;
}

bool HistoryStore::duplicateKeys(QSet<QByteArray> *keys, QString *error)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (!exec(query, QStringLiteral("SELECT type, direction, startTime, endTime, remoteUid, freeText, "
                                    "isMissedCall FROM Events WHERE type IN (2, 3, 6)"), error))
        return false;
    while (query.next()) {
        HistoryEvent e;
        e.type = query.value(0).toInt();
        e.direction = query.value(1).toInt();
        e.startTime = query.value(2).toLongLong();
        e.endTime = query.value(3).toLongLong();
        e.remoteUid = query.value(4).toString();
        e.freeText = query.value(5).toString();
        e.isMissedCall = query.value(6).toInt() != 0;
        keys->insert(Mapping::duplicateKey(e));
    }
    return true;
}

bool HistoryStore::groupsOf(const QString &localUid, const std::function<QString(const QString &)> &keyOf,
                            QHash<QString, int> *groups, QString *error)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.setForwardOnly(true);
    query.prepare(QStringLiteral("SELECT id, remoteUids FROM Groups WHERE localUid = ? ORDER BY id"));
    query.addBindValue(localUid);
    if (!query.exec()) {
        *error = tr("Reading the conversations failed: %1").arg(query.lastError().text());
        return false;
    }
    while (query.next()) {
        const QStringList remoteUids = query.value(1).toString().split(QLatin1Char('\n'));
        if (remoteUids.size() != 1 || remoteUids.first().isEmpty())
            continue;   // group chats: Sailfish OS files SMS only into one-to-one conversations
        const QString key = keyOf(remoteUids.first());
        if (!groups->contains(key))
            groups->insert(key, query.value(0).toInt());
    }
    return true;
}

QStringList HistoryStore::cellularAccounts()
{
    QStringList accounts;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.setForwardOnly(true);
    query.prepare(QStringLiteral("SELECT localUid, MAX(id) AS newest FROM Events WHERE localUid LIKE ? "
                                 "GROUP BY localUid ORDER BY newest DESC"));
    query.addBindValue(RingAccountPattern);
    if (query.exec()) {
        while (query.next())
            accounts.append(query.value(0).toString());
    }
    return accounts;
}

QString HistoryStore::subscriberIdentity(const QString &localUid)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.setForwardOnly(true);
    query.prepare(QStringLiteral("SELECT p.value FROM EventProperties p JOIN Events e ON e.id = p.eventId "
                                 "WHERE p.key = 'subscriberIdentity' AND e.localUid = ? "
                                 "ORDER BY e.id DESC LIMIT 1"));
    query.addBindValue(localUid);
    if (query.exec() && query.next())
        return query.value(0).toString();
    return QString();
}
