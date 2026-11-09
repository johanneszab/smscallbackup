// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef SBRREADER_H
#define SBRREADER_H

#include "sbrrecords.h"

#include <QString>
#include <functional>

// Reads an SMS Backup & Restore file (.xml or .xml.zip) record by record. The file is
// parsed incrementally, so large backups with MMS attachments only need memory for one
// message at a time. Invalid character references written by the Android app are
// repaired while reading (see XmlRepairDevice).
class SbrReader
{
public:
    enum Kind {
        UnknownKind,
        MessagesKind,   // <smses>: SMS and MMS
        CallsKind       // <calls>: call log
    };

    // Each callback returns false to stop reading (cancel).
    struct Callbacks
    {
        std::function<bool(const Sbr::SmsRecord &)> sms;
        std::function<bool(const Sbr::MmsRecord &)> mms;
        std::function<bool(const Sbr::CallRecord &)> call;
        std::function<bool(qint64 done, qint64 total)> progress;
    };

    // What the root element says about a file, without reading the records.
    struct Info
    {
        bool ok = false;
        QString error;
        Kind kind = UnknownKind;
        int count = -1;             // the root's count attribute, -1 if missing
        qint64 backupDate = 0;      // ms since the epoch, 0 if missing
        QString backupSet;
        QString type;               // "full", "archive" (added to with each backup), or empty
        bool compressed = false;
        bool isArchive() const { return type == QLatin1String("archive"); }
    };

    explicit SbrReader(const QString &path);

    bool read(const Callbacks &callbacks);

    QString errorString() const { return m_error; }
    bool wasCancelled() const { return m_cancelled; }
    const Info &info() const { return m_info; }
    int repairCount() const { return m_repairs; }

    static Info probe(const QString &path);

private:
    bool run(const Callbacks *callbacks);

    QString m_path;
    QString m_error;
    Info m_info;
    bool m_cancelled = false;
    int m_repairs = 0;
};

#endif // SBRREADER_H
