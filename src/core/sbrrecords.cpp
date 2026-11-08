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

QString MmsPart::value(const QString &name) const
{
    return attributeValue(attributes, name);
}

QString MmsRecord::value(const QString &name) const
{
    return attributeValue(attributes, name);
}

} // namespace Sbr
