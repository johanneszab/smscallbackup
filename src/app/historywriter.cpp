// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "historywriter.h"

#include "historystore.h"

#include <CommHistory/Event>
#include <CommHistory/EventModel>
#include <CommHistory/Group>
#include <CommHistory/MessagePart>
#include <CommHistory/commhistorydatabasepath.h>
#include <CommHistory/groupmanager.h>
#include <CommHistory/Recipient>
#include <CommHistory/commonutils.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <QUuid>

namespace {

const int BatchSize = 200;
const QLatin1String Rejected("rejected");
const QLatin1String Ignored("ignored");

QString randomHex(int length)
{
    QByteArray hex;
    while (hex.size() < length)
        hex += QUuid::createUuid().toRfc4122().toHex();
    return QString::fromLatin1(hex.left(length).toUpper());
}

// Moves the staged files of an MMS into the event's folder, data/<event id>/, the way
// commhistoryd stores the parts of the MMS it receives.
bool storeParts(const HistoryEvent &h, int eventId, QList<CommHistory::MessagePart> *parts, QString *error)
{
    const QString dir = CommHistoryDatabasePath::dataDir(eventId);
    if (!QDir().mkpath(dir)) {
        *error = QCoreApplication::translate("HistoryWriter", "Cannot create the folder %1.").arg(dir);
        return false;
    }
    QSet<QString> used;
    for (const HistoryPart &p : h.parts) {
        const QString name = p.fileName.isEmpty() ? QStringLiteral("part") : p.fileName;
        QString unique = name;
        for (int n = 2; used.contains(unique.toLower()); ++n) {
            const int dot = name.lastIndexOf(QLatin1Char('.'));
            unique = dot > 0 ? QStringLiteral("%1-%2%3").arg(name.left(dot)).arg(n).arg(name.mid(dot))
                             : QStringLiteral("%1-%2").arg(name).arg(n);
        }
        used.insert(unique.toLower());
        const QString target = QDir(dir).filePath(unique);
        if (QFileInfo::exists(target))
            QFile::remove(target);
        // QFile::rename() copies if the files are on different file systems.
        if (!QFile::rename(p.path, target)) {
            *error = QCoreApplication::translate("HistoryWriter", "Storing the attachment %1 failed.").arg(unique);
            return false;
        }
        CommHistory::MessagePart part;
        part.setContentId(p.contentId);
        part.setContentType(p.contentType);
        part.setPath(target);
        parts->append(part);
    }
    return true;
}

} // namespace

HistoryWriter::HistoryWriter(QObject *parent)
    : QObject(parent)
{
}

HistoryWriter::~HistoryWriter()
{
}

QString HistoryWriter::groupKey(const QString &remoteUid)
{
    // Same as libcommhistory's minimizeRemoteUid() for cellular accounts.
    const QString minimized = CommHistory::minimizePhoneNumber(remoteUid).toLower();
    return minimized.isEmpty() ? remoteUid.toLower() : minimized;
}

QString HistoryWriter::conversationKey(const QStringList &remoteUids)
{
    QStringList keys;
    for (const QString &uid : remoteUids)
        keys.append(groupKey(uid));
    keys.removeDuplicates();
    keys.sort();
    return keys.join(QLatin1Char('\n'));
}

QString HistoryWriter::conversationKey(const HistoryEvent &event)
{
    return event.participants.size() > 1 ? conversationKey(event.participants) : groupKey(event.remoteUid);
}

void HistoryWriter::start(const QList<HistoryEvent> &events, const QString &localUid,
                          const QString &subscriberIdentity)
{
    if (m_running)
        return;
    m_events = events;
    m_localUid = localUid;
    m_subscriberIdentity = subscriberIdentity;
    // Sent SMS get a token like the ones of the cellular connection manager, "/ril_0/message_<hex>".
    m_tokenPrefix = QLatin1Char('/') + localUid.section(QLatin1Char('/'), -1) + QStringLiteral("/message_");
    m_next = 0;
    m_messages = 0;
    m_calls = 0;
    m_conversations = 0;
    m_cancel = false;
    m_running = true;

    m_model.reset(new CommHistory::EventModel);
    m_model->setDefaultAccept(false);   // only write, do not keep the events in memory
    m_model->setResolveContacts(CommHistory::EventModel::DoNotResolve);
    m_groupManager.reset(new CommHistory::GroupManager);
    m_groupManager->setResolveContacts(CommHistory::GroupManager::DoNotResolve);

    QString error;
    if (!prepareConversations(&error)) {
        QMetaObject::invokeMethod(this, "finish", Qt::QueuedConnection, Q_ARG(bool, false), Q_ARG(QString, error));
        return;
    }
    QTimer::singleShot(0, this, SLOT(step()));
}

void HistoryWriter::cancel()
{
    m_cancel = true;
}

bool HistoryWriter::prepareConversations(QString *error)
{
    // Read the conversations only now, right before writing, so a message that arrived in
    // the meantime does not lead to a second conversation with the same person.
    m_groups.clear();
    if (HistoryStore::exists()) {
        HistoryStore store;
        const auto keyOf = [](const QStringList &remoteUids) { return HistoryWriter::conversationKey(remoteUids); };
        if (!store.open(error) || !store.conversations(m_localUid, keyOf, &m_groups, error))
            return false;
    }

    // New one-to-one conversations are named after the number's international form if
    // the backup has it (like "+49176..." rather than "0176..."), else after its most
    // used form. Group conversations get the members of their first message.
    QHash<QString, QHash<QString, int> > forms;
    QHash<QString, QStringList> members;
    QStringList newKeys;
    for (const HistoryEvent &e : m_events) {
        if (!e.isMessage())
            continue;
        const QString key = conversationKey(e);
        if (m_groups.contains(key))
            continue;
        if (!forms.contains(key) && !members.contains(key))
            newKeys.append(key);
        if (e.participants.size() > 1) {
            if (!members.contains(key))
                members.insert(key, e.participants);
        } else {
            forms[key][e.remoteUid] += 1;
        }
    }
    if (newKeys.isEmpty())
        return true;

    QList<CommHistory::Group> groups;
    for (const QString &key : newKeys) {
        CommHistory::Group group;
        group.setLocalUid(m_localUid);
        if (members.contains(key)) {
            // Like the Messages app makes conversations with several people.
            group.setRecipients(CommHistory::RecipientList::fromUids(m_localUid, members.value(key)));
        } else {
            const QHash<QString, int> &candidates = forms[key];
            QString best;
            int bestCount = 0;
            bool bestInternational = false;
            for (QHash<QString, int>::const_iterator it = candidates.constBegin(); it != candidates.constEnd(); ++it) {
                const bool international = it.key().startsWith(QLatin1Char('+'));
                if (best.isNull() || (international && !bestInternational)
                        || (international == bestInternational && it.value() > bestCount)) {
                    best = it.key();
                    bestCount = it.value();
                    bestInternational = international;
                }
            }
            group.setRecipients(CommHistory::RecipientList(CommHistory::Recipient(m_localUid, best)));
        }
        group.setChatType(CommHistory::Group::ChatTypeP2P);
        groups.append(group);
    }
    if (!m_groupManager->addGroups(groups)) {
        *error = tr("Adding the conversations to the message database failed.");
        return false;
    }
    for (int i = 0; i < groups.size(); ++i)
        m_groups.insert(newKeys.at(i), groups.at(i).id());
    m_conversations = groups.size();
    return true;
}

void HistoryWriter::step()
{
    if (!m_running)
        return;
    if (m_cancel) {
        finish(false, tr("Cancelled."));
        return;
    }

    using CommHistory::Event;
    QList<Event> batch;
    QList<int> indexes;     // m_events index of each batch entry
    int messages = 0;
    int calls = 0;
    const int end = qMin(m_next + BatchSize, m_events.size());
    for (int i = m_next; i < end; ++i) {
        const HistoryEvent &h = m_events.at(i);
        Event e;
        e.setType(Event::EventType(h.type));
        e.setDirection(Event::EventDirection(h.direction));
        e.setStartTimeT(quint32(h.startTime));
        e.setEndTimeT(quint32(h.endTime));
        e.setLocalUid(m_localUid);
        e.setRecipients(CommHistory::RecipientList(CommHistory::Recipient(m_localUid, h.remoteUid)));
        e.setIsRead(h.isRead);
        e.setStatus(Event::EventStatus(h.status));
        if (h.isCall()) {
            // What Event::setIncomingStatus() does; that method only exists since
            // libcommhistory 1.12.6, the property is understood by all versions.
            e.setIsMissedCall(h.isMissedCall || h.incomingStatus == Rejected || h.incomingStatus == Ignored);
            if (h.incomingStatus == Rejected || h.incomingStatus == Ignored)
                e.setExtraProperty(QStringLiteral("incomingStatus"), h.incomingStatus);
            e.setLastModifiedT(quint32(h.startTime));
            ++calls;
        } else {
            const int groupId = m_groups.value(conversationKey(h), -1);
            if (groupId < 0) {
                finish(false, tr("No conversation for %1.").arg(h.remoteUid));
                return;
            }
            e.setGroupId(groupId);
            e.setFreeText(h.freeText);
            if (!h.subject.isEmpty())
                e.setSubject(h.subject);
            if (h.isMms()) {
                // Like the MMS commhistoryd stores: headers, no message token; the
                // attachments follow once the event has its id.
                if (!h.toList.isEmpty())
                    e.setToList(h.toList);
                if (!h.ccList.isEmpty())
                    e.setCcList(h.ccList);
                if (!h.bccList.isEmpty())
                    e.setBccList(h.bccList);
                if (!h.mmsMessageId.isEmpty())
                    e.setExtraProperty(QLatin1String(HistoryStore::MmsMessageIdProperty), h.mmsMessageId);
                if (!h.mmsTransactionId.isEmpty())
                    e.setExtraProperty(QLatin1String(HistoryStore::MmsTransactionIdProperty), h.mmsTransactionId);
                e.setLastModifiedT(quint32(h.endTime));
            } else if (h.direction == HistoryEvent::Inbound) {
                // The same tokens and modification times as messages the phone handled itself.
                e.setMessageToken(QUuid::createUuid().toString().mid(1, 36));
                e.setLastModifiedT(0);
            } else {
                e.setMessageToken(m_tokenPrefix + randomHex(40));
                e.setLastModifiedT(quint32(h.endTime));
            }
            ++messages;
        }
        if (!m_subscriberIdentity.isEmpty())
            e.setSubscriberIdentity(m_subscriberIdentity);
        batch.append(e);
        indexes.append(i);
    }

    if (!batch.isEmpty() && !m_model->addEvents(batch, false)) {
        finish(false, tr("Writing to the message database failed."));
        return;
    }

    QList<Event> withParts;
    for (int b = 0; b < batch.size(); ++b) {
        const HistoryEvent &h = m_events.at(indexes.at(b));
        if (!h.isMms() || h.parts.isEmpty())
            continue;
        QList<CommHistory::MessagePart> parts;
        QString error;
        if (!storeParts(h, batch.at(b).id(), &parts, &error)) {
            m_messages += messages;
            m_calls += calls;
            finish(false, error);
            return;
        }
        Event e = batch.at(b);
        e.setMessageParts(parts);
        withParts.append(e);
    }
    if (!withParts.isEmpty() && !m_model->modifyEvents(withParts)) {
        m_messages += messages;
        m_calls += calls;
        finish(false, tr("Writing to the message database failed."));
        return;
    }

    m_messages += messages;
    m_calls += calls;
    m_next = end;
    emit progress(m_next, m_events.size());

    if (m_next >= m_events.size())
        finish(true, QString());
    else
        QTimer::singleShot(0, this, SLOT(step()));
}

void HistoryWriter::finish(bool ok, const QString &error)
{
    if (!m_running)
        return;
    m_running = false;
    // libcommhistory sends its change signals asynchronously. A blocking call to the bus
    // goes out after them, so they have left this process when it returns (matters for
    // the command line, which exits right after).
    QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.freedesktop.DBus"));
    emit finished(ok, m_messages, m_calls, m_conversations, error);
}
