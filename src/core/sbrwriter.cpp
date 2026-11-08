// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "sbrwriter.h"

#include <QCoreApplication>
#include <QLocale>

namespace {

void attribute(QByteArray &line, const char *name, const QString &value)
{
    line += ' ';
    line += name;
    line += "=\"";
    line += SbrWriter::escape(value);
    line += '"';
}

void attribute(QByteArray &line, const char *name, qint64 value)
{
    line += ' ';
    line += name;
    line += "=\"";
    line += QByteArray::number(value);
    line += '"';
}

void attributes(QByteArray &line, const Sbr::AttributeList &list)
{
    for (const QPair<QString, QString> &a : list) {
        line += ' ';
        line += a.first.toUtf8();
        line += "=\"";
        line += SbrWriter::escape(a.second);
        line += '"';
    }
}

} // namespace

SbrWriter::SbrWriter(QIODevice *device, Kind kind)
    : m_device(device)
    , m_kind(kind)
{
}

QByteArray SbrWriter::escape(const QString &value)
{
    if (value.isNull())
        return QByteArrayLiteral("null");
    QString out;
    out.reserve(value.size() + 8);
    const int size = value.size();
    for (int i = 0; i < size; ++i) {
        const QChar c = value.at(i);
        const ushort u = c.unicode();
        switch (u) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        case '\t': out += QLatin1String("&#9;"); break;
        case '\n': out += QLatin1String("&#10;"); break;
        case '\r': out += QLatin1String("&#13;"); break;
        default:
            if (u < 0x20 || u == 0xFFFE || u == 0xFFFF) {
                // Not representable in XML 1.0, not even as a reference: dropped
            } else if (c.isHighSurrogate()) {
                if (i + 1 < size && value.at(i + 1).isLowSurrogate()) {
                    out += c;
                    out += value.at(++i);
                } else {
                    out += QChar(0xFFFD);
                }
            } else if (c.isLowSurrogate()) {
                out += QChar(0xFFFD);
            } else {
                out += c;
            }
        }
    }
    return out.toUtf8();
}

bool SbrWriter::write(const QByteArray &data)
{
    if (!m_error.isEmpty())
        return false;
    if (m_device->write(data) != data.size()) {
        m_error = m_device->errorString();
        if (m_error.isEmpty())
            m_error = QCoreApplication::translate("SbrWriter", "Writing the backup failed.");
        return false;
    }
    return true;
}

bool SbrWriter::begin(int count, const QString &backupSet, qint64 backupDate, const QString &creator)
{
    const QDateTime created = QDateTime::fromMSecsSinceEpoch(backupDate).toLocalTime();
    QByteArray header("<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>\n");
    QString comment = QStringLiteral("File Created By %1 on %2")
            .arg(creator, QLocale::c().toString(created, QStringLiteral("dd/MM/yyyy HH:mm:ss")));
    comment.replace(QLatin1String("--"), QLatin1String("- -"));   // not allowed in comments
    header += "<!--" + comment.toUtf8() + "-->\n";
    header += m_kind == MessagesKind ? "<smses" : "<calls";
    attribute(header, "count", count);
    attribute(header, "backup_set", backupSet);
    attribute(header, "backup_date", backupDate);
    header += ">\n";
    return write(header);
}

bool SbrWriter::writeSms(const Sbr::SmsRecord &r)
{
    QByteArray line("  <sms");
    attribute(line, "protocol", r.protocol);
    attribute(line, "address", r.address);
    attribute(line, "date", r.date);
    attribute(line, "type", r.type);
    attribute(line, "subject", r.subject);
    attribute(line, "body", r.body);
    attribute(line, "toa", r.toa);
    attribute(line, "sc_toa", r.scToa);
    attribute(line, "service_center", r.serviceCenter);
    attribute(line, "read", r.read);
    attribute(line, "status", r.status);
    attribute(line, "locked", r.locked);
    attribute(line, "date_sent", r.dateSent);
    attribute(line, "sub_id", r.subId);
    if (!r.readableDate.isNull())
        attribute(line, "readable_date", r.readableDate);
    if (!r.contactName.isNull())
        attribute(line, "contact_name", r.contactName);
    line += " />\n";
    return write(line);
}

bool SbrWriter::writeCall(const Sbr::CallRecord &r)
{
    QByteArray line("  <call");
    attribute(line, "number", r.number);
    attribute(line, "duration", r.duration);
    attribute(line, "date", r.date);
    attribute(line, "type", r.type);
    attribute(line, "presentation", r.presentation);
    attribute(line, "subscription_id", r.subscriptionId);
    attribute(line, "post_dial_digits", r.postDialDigits.isNull() ? QString(QLatin1String("")) : r.postDialDigits);
    attribute(line, "subscription_component_name", r.subscriptionComponentName);
    if (!r.readableDate.isNull())
        attribute(line, "readable_date", r.readableDate);
    if (!r.contactName.isNull())
        attribute(line, "contact_name", r.contactName);
    line += " />\n";
    return write(line);
}

bool SbrWriter::writeMms(const Sbr::MmsRecord &r)
{
    QByteArray data("  <mms");
    attributes(data, r.attributes);
    data += ">\n    <parts>\n";
    for (const Sbr::MmsPart &part : r.parts) {
        data += "      <part";
        attributes(data, part.attributes);
        data += " />\n";
    }
    data += "    </parts>\n    <addrs>\n";
    for (const Sbr::MmsAddress &address : r.addresses) {
        data += "      <addr";
        attribute(data, "address", address.address);
        attribute(data, "type", address.type);
        attribute(data, "charset", address.charset);
        data += " />\n";
    }
    data += "    </addrs>\n  </mms>\n";
    return write(data);
}

bool SbrWriter::end()
{
    return write(m_kind == MessagesKind ? QByteArray("</smses>\n") : QByteArray("</calls>\n"));
}
