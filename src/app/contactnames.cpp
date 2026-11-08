// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "contactnames.h"

#include "mapping.h"

#include <CommHistory/ContactResolver>

#include <QFileInfo>
#include <QStandardPaths>

namespace {
const int TimeoutMs = 60 * 1000;
}

bool ContactNames::available()
{
    // Where qtcontacts-sqlite keeps the privileged database.
    const QFileInfo database(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                             + QStringLiteral("/system/privileged/Contacts/qtcontacts-sqlite/contacts.db"));
    return database.exists() && database.isReadable();
}

ContactNames::ContactNames(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(TimeoutMs);
    connect(&m_timeout, SIGNAL(timeout()), this, SLOT(done()));
}

void ContactNames::resolve(const QStringList &phoneNumbers)
{
    m_finished = false;
    m_recipients.clear();
    if (!available()) {
        QMetaObject::invokeMethod(this, "done", Qt::QueuedConnection);
        return;
    }
    if (!m_resolver) {
        m_resolver = new CommHistory::ContactResolver(this);
        m_resolver->setForceResolving(true);
        connect(m_resolver, SIGNAL(finished()), this, SLOT(done()));
    }
    // Any cellular account works: phone numbers are matched without the account.
    const QString account = Mapping::ringAccount(1);
    QList<CommHistory::Recipient> recipients;
    for (const QString &number : phoneNumbers) {
        if (number.isEmpty())
            continue;
        const CommHistory::Recipient recipient(account, number);
        m_recipients.append(qMakePair(number, recipient));
        recipients.append(recipient);
    }
    if (recipients.isEmpty()) {
        QMetaObject::invokeMethod(this, "done", Qt::QueuedConnection);
        return;
    }
    m_timeout.start();
    m_resolver->add(recipients);
}

QHash<QString, QString> ContactNames::names() const
{
    QHash<QString, QString> result;
    for (const QPair<QString, CommHistory::Recipient> &entry : m_recipients) {
        const QString name = entry.second.contactName();
        if (!name.isEmpty())
            result.insert(entry.first, name);
    }
    return result;
}

void ContactNames::done()
{
    if (m_finished)
        return;
    m_finished = true;
    m_timeout.stop();
    emit finished();
}
