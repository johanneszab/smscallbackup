// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "sbrreader.h"

#include "xmlrepairdevice.h"
#include "zipentrydevice.h"

#include <QCoreApplication>
#include <QFile>
#include <QScopedPointer>
#include <QXmlStreamReader>

namespace {

const int ProgressInterval = 250;   // records between progress callbacks

QString tr(const char *text)
{
    return QCoreApplication::translate("SbrReader", text);
}

// Attribute value; missing attributes and "null" give a null string.
QString text(const QXmlStreamAttributes &attributes, const char *name)
{
    const QLatin1String key(name);
    if (!attributes.hasAttribute(key))
        return QString();
    const QString value = attributes.value(key).toString();
    return value == QLatin1String("null") ? QString() : value;
}

qint64 number(const QXmlStreamAttributes &attributes, const char *name, qint64 fallback)
{
    bool ok = false;
    const qint64 value = attributes.value(QLatin1String(name)).toString().trimmed().toLongLong(&ok);
    return ok ? value : fallback;
}

Sbr::AttributeList attributeList(const QXmlStreamAttributes &attributes)
{
    Sbr::AttributeList list;
    list.reserve(attributes.size());
    for (const QXmlStreamAttribute &attribute : attributes)
        list.append(qMakePair(attribute.name().toString(), attribute.value().toString()));
    return list;
}

Sbr::SmsRecord smsRecord(const QXmlStreamAttributes &a)
{
    Sbr::SmsRecord r;
    r.protocol = int(number(a, "protocol", 0));
    r.address = text(a, "address");
    r.date = number(a, "date", 0);
    r.type = int(number(a, "type", 0));
    r.subject = text(a, "subject");
    r.body = text(a, "body");
    r.toa = text(a, "toa");
    r.scToa = text(a, "sc_toa");
    r.serviceCenter = text(a, "service_center");
    r.read = int(number(a, "read", 1));
    r.status = int(number(a, "status", Sbr::SmsStatusNone));
    r.locked = int(number(a, "locked", 0));
    r.dateSent = number(a, "date_sent", 0);
    r.subId = text(a, "sub_id");
    r.readableDate = text(a, "readable_date");
    r.contactName = text(a, "contact_name");
    r.attributes = attributeList(a);
    return r;
}

Sbr::CallRecord callRecord(const QXmlStreamAttributes &a)
{
    Sbr::CallRecord r;
    r.number = text(a, "number");
    r.duration = number(a, "duration", 0);
    r.date = number(a, "date", 0);
    r.type = int(number(a, "type", 0));
    r.presentation = int(number(a, "presentation", Sbr::PresentationAllowed));
    r.subscriptionId = text(a, "subscription_id");
    r.postDialDigits = text(a, "post_dial_digits");
    r.subscriptionComponentName = text(a, "subscription_component_name");
    r.readableDate = text(a, "readable_date");
    r.contactName = text(a, "contact_name");
    r.attributes = attributeList(a);
    return r;
}

class Source
{
public:
    bool open(const QString &path, QString *error, bool *compressed)
    {
        *compressed = ZipEntryDevice::isZipFile(path);
        if (*compressed) {
            m_zip.reset(new ZipEntryDevice(path));
            if (!m_zip->open(QIODevice::ReadOnly)) {
                *error = m_zip->errorString();
                return false;
            }
            m_device = m_zip.data();
        } else {
            m_file.reset(new QFile(path));
            if (!m_file->open(QIODevice::ReadOnly)) {
                *error = m_file->errorString();
                return false;
            }
            m_device = m_file.data();
        }
        m_repair.reset(new XmlRepairDevice(m_device));
        return true;
    }

    QIODevice *device() const { return m_repair.data(); }
    QIODevice *raw() const { return m_device; }
    qint64 position() const { return m_zip ? m_zip->archivePosition() : m_file->pos(); }
    qint64 size() const { return m_zip ? m_zip->archiveSize() : m_file->size(); }
    int repairs() const { return m_repair ? m_repair->repairCount() : 0; }

private:
    QScopedPointer<QFile> m_file;
    QScopedPointer<ZipEntryDevice> m_zip;
    QScopedPointer<XmlRepairDevice> m_repair;
    QIODevice *m_device = nullptr;
};

} // namespace

SbrReader::SbrReader(const QString &path)
    : m_path(path)
{
}

SbrReader::Info SbrReader::probe(const QString &path)
{
    SbrReader reader(path);
    reader.run(nullptr);
    return reader.m_info;
}

bool SbrReader::read(const Callbacks &callbacks)
{
    return run(&callbacks);
}

bool SbrReader::run(const Callbacks *callbacks)
{
    m_error.clear();
    m_cancelled = false;
    m_info = Info();

    Source source;
    QString openError;
    if (!source.open(m_path, &openError, &m_info.compressed)) {
        m_error = tr("Cannot open %1: %2").arg(m_path, openError);
        m_info.error = m_error;
        return false;
    }

    QXmlStreamReader xml(source.device());
    bool haveRoot = false;
    int records = 0;
    const qint64 total = source.size();

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token != QXmlStreamReader::StartElement)
            continue;

        if (!haveRoot) {
            haveRoot = true;
            const QXmlStreamAttributes a = xml.attributes();
            if (xml.name() == QLatin1String("smses"))
                m_info.kind = MessagesKind;
            else if (xml.name() == QLatin1String("calls"))
                m_info.kind = CallsKind;
            else
                break;
            m_info.count = int(number(a, "count", -1));
            m_info.backupDate = number(a, "backup_date", 0);
            m_info.backupSet = text(a, "backup_set");
            m_info.type = text(a, "type");
            m_info.ok = true;
            if (!callbacks)
                break;
            continue;
        }

        bool keepGoing = true;
        if (xml.name() == QLatin1String("sms") && m_info.kind == MessagesKind) {
            const Sbr::SmsRecord record = smsRecord(xml.attributes());
            xml.skipCurrentElement();
            ++records;
            if (callbacks->sms)
                keepGoing = callbacks->sms(record);
        } else if (xml.name() == QLatin1String("call") && m_info.kind == CallsKind) {
            const Sbr::CallRecord record = callRecord(xml.attributes());
            xml.skipCurrentElement();
            ++records;
            if (callbacks->call)
                keepGoing = callbacks->call(record);
        } else if (xml.name() == QLatin1String("mms") && m_info.kind == MessagesKind) {
            Sbr::MmsRecord record;
            record.attributes = attributeList(xml.attributes());
            // <mms> holds <parts><part/>...</parts> and <addrs><addr/>...</addrs>
            int depth = 1;
            while (depth > 0 && !xml.atEnd()) {
                const QXmlStreamReader::TokenType inner = xml.readNext();
                if (inner == QXmlStreamReader::EndElement) {
                    --depth;
                } else if (inner == QXmlStreamReader::StartElement) {
                    if (xml.name() == QLatin1String("part")) {
                        Sbr::MmsPart part;
                        part.attributes = attributeList(xml.attributes());
                        record.parts.append(part);
                        xml.skipCurrentElement();
                    } else if (xml.name() == QLatin1String("addr")) {
                        const QXmlStreamAttributes a = xml.attributes();
                        Sbr::MmsAddress address;
                        address.address = text(a, "address");
                        address.type = int(number(a, "type", 0));
                        address.charset = int(number(a, "charset", 106));
                        record.addresses.append(address);
                        xml.skipCurrentElement();
                    } else if (xml.name() == QLatin1String("parts") || xml.name() == QLatin1String("addrs")) {
                        ++depth;
                    } else {
                        xml.skipCurrentElement();
                    }
                }
            }
            ++records;
            if (callbacks->mms && !xml.hasError())
                keepGoing = callbacks->mms(record);
        } else {
            xml.skipCurrentElement();   // unknown element: newer versions may add some
        }

        if (keepGoing && callbacks->progress && records % ProgressInterval == 0)
            keepGoing = callbacks->progress(source.position(), total);
        if (!keepGoing) {
            m_cancelled = true;
            m_repairs = source.repairs();
            return false;
        }
    }

    m_repairs = source.repairs();

    if (!haveRoot || m_info.kind == UnknownKind) {
        m_error = tr("%1 is not a backup of SMS Backup & Restore.").arg(m_path);
        m_info.ok = false;
        m_info.error = m_error;
        return false;
    }
    if (!callbacks)
        return m_info.ok;

    if (xml.hasError()) {
        // A damaged zip shows up as a read error of the underlying device.
        const QString deviceError = source.raw()->errorString();
        if (m_info.compressed && !deviceError.isEmpty() && deviceError != QLatin1String("Unknown error"))
            m_error = tr("%1 is damaged: %2").arg(m_path, deviceError);
        else
            m_error = tr("%1 is not a valid backup file (line %2: %3).")
                    .arg(m_path).arg(xml.lineNumber()).arg(xml.errorString());
        return false;
    }
    if (callbacks->progress)
        callbacks->progress(total, total);
    return true;
}
