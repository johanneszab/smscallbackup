// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef CONTACTNAMES_H
#define CONTACTNAMES_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QTimer>

#include <CommHistory/Recipient>

namespace CommHistory {
class ContactResolver;
}

// Looks up the contact names of phone numbers for the contact_name attribute of the
// backup, with the same matching as the Messages app (libcommhistory's ContactResolver).
//
// The contacts database is privileged data: only processes in the "privileged" group can
// read it (a sandboxed third-party app cannot). Without access nothing is looked up, so
// qtcontacts-sqlite does not create its empty non-privileged database instead.
class ContactNames : public QObject
{
    Q_OBJECT

public:
    explicit ContactNames(QObject *parent = nullptr);

    // True if this process can read the contacts database.
    static bool available();

    // Asynchronous; emits finished() when done or after a timeout.
    void resolve(const QStringList &phoneNumbers);
    // phone number as given to resolve() -> contact name; numbers without contact are left out
    QHash<QString, QString> names() const;

signals:
    void finished();

private slots:
    void done();

private:
    CommHistory::ContactResolver *m_resolver = nullptr;
    QList<QPair<QString, CommHistory::Recipient> > m_recipients;
    QTimer m_timeout;
    bool m_finished = false;
};

#endif // CONTACTNAMES_H
