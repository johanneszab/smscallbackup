// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef SBRWRITER_H
#define SBRWRITER_H

#include "sbrrecords.h"

#include <QByteArray>
#include <QDateTime>
#include <QIODevice>

// Writes a backup in the XML format of SMS Backup & Restore, laid out like the files of the
// Android app (one record per line, attributes in the app's order, "null" for unknown
// values), so the Android app can restore it and its online viewer can show it.
//
// The output is always well-formed XML 1.0 in UTF-8: unlike the Android app, characters
// outside the Basic Multilingual Plane are written as UTF-8, not as surrogate references.
class SbrWriter
{
public:
    enum Kind {
        MessagesKind,   // <smses>
        CallsKind       // <calls>
    };

    SbrWriter(QIODevice *device, Kind kind);

    // count: number of records that will follow; backupSet: an id shared by the files of
    // one backup run; backupDate: ms since the epoch; creator: e.g. "SMS & Call Backup 1.0".
    bool begin(int count, const QString &backupSet, qint64 backupDate, const QString &creator);
    bool writeSms(const Sbr::SmsRecord &record);
    bool writeCall(const Sbr::CallRecord &record);
    bool writeMms(const Sbr::MmsRecord &record);
    bool end();

    QString errorString() const { return m_error; }

    // Attribute value as UTF-8 with XML escapes; a null string gives "null".
    static QByteArray escape(const QString &value);

private:
    bool write(const QByteArray &data);

    QIODevice *m_device;
    Kind m_kind;
    QString m_error;
};

#endif // SBRWRITER_H
