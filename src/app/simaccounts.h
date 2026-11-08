// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef SIMACCOUNTS_H
#define SIMACCOUNTS_H

#include <QStringList>

namespace SimAccounts {

// The phone's cellular Telepathy accounts (.../ring/tel/ril_0 for SIM 1, ril_1 for SIM 2),
// as the account manager lists them. Empty if it cannot be asked.
QStringList telepathyAccounts();

// The accounts a restore can use: those with history first (most recently used first),
// then the other SIMs of the phone. Never empty.
QStringList restoreTargets(const QStringList &withHistory);

// "SIM 1", "SIM 2", or the account path for anything unusual.
QString displayName(const QString &localUid);

} // namespace SimAccounts

#endif // SIMACCOUNTS_H
