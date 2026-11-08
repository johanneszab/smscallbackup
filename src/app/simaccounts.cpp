// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "simaccounts.h"

#include "mapping.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <algorithm>

namespace {
const QLatin1String RingPrefix("/org/freedesktop/Telepathy/Account/ring/tel/");
}

namespace SimAccounts {

QStringList telepathyAccounts()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.Telepathy.AccountManager"),
                QStringLiteral("/org/freedesktop/Telepathy/AccountManager"),
                QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    call << QStringLiteral("org.freedesktop.Telepathy.AccountManager") << QStringLiteral("ValidAccounts");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    QStringList accounts;
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return accounts;
    const QVariant value = reply.arguments().first().value<QDBusVariant>().variant();
    QList<QDBusObjectPath> paths;
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        argument >> paths;
    }
    for (const QDBusObjectPath &path : paths) {
        if (path.path().startsWith(RingPrefix) && Mapping::simSlot(path.path()) > 0)
            accounts.append(path.path());
    }
    std::sort(accounts.begin(), accounts.end());
    return accounts;
}

QStringList restoreTargets(const QStringList &withHistory)
{
    QStringList targets;
    for (const QString &account : withHistory) {
        if (Mapping::simSlot(account) > 0 && !targets.contains(account))
            targets.append(account);
    }
    for (const QString &account : telepathyAccounts()) {
        if (!targets.contains(account))
            targets.append(account);
    }
    if (targets.isEmpty())
        targets.append(Mapping::ringAccount(1));
    return targets;
}

QString displayName(const QString &localUid)
{
    const int slot = Mapping::simSlot(localUid);
    if (slot > 0)
        return QCoreApplication::translate("SimAccounts", "SIM %1").arg(slot);
    return localUid;
}

} // namespace SimAccounts
