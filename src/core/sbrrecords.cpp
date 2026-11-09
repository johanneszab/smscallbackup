// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "sbrrecords.h"

namespace Sbr {

QString attributeValue(const AttributeList &attributes, const QString &name)
{
    for (const QPair<QString, QString> &attribute : attributes) {
        if (attribute.first == name)
            return attribute.second == QLatin1String("null") ? QString() : attribute.second;
    }
    return QString();
}

void setAttribute(AttributeList *attributes, const QString &name, const QString &value)
{
    for (QPair<QString, QString> &attribute : *attributes) {
        if (attribute.first == name) {
            attribute.second = value;
            return;
        }
    }
    attributes->append(qMakePair(name, value));
}

QString MmsPart::value(const QString &name) const
{
    return attributeValue(attributes, name);
}

QString MmsRecord::value(const QString &name) const
{
    return attributeValue(attributes, name);
}

qint64 MmsRecord::number(const QString &name, qint64 fallback) const
{
    bool ok = false;
    const qint64 result = value(name).trimmed().toLongLong(&ok);
    return ok ? result : fallback;
}

} // namespace Sbr
