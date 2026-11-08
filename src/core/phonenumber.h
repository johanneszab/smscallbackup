// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef PHONENUMBER_H
#define PHONENUMBER_H

#include <QString>

namespace PhoneNumber {

// The short form Sailfish OS uses to decide whether two phone numbers are the same
// (qtcontacts-sqlite's minimizePhoneNumber with the dial string kept): digits only, '+'
// only at the start, and only the last maxDigits digits before any DTMF part.
// "+49 176 1234-5678", "0176 12345678" and "017612345678" all give "12345678".
QString minimize(const QString &number, int maxDigits = 8);

// Comparison key of a remote address on a cellular account, as libcommhistory computes
// it: the minimized number, or the lower-cased text for alphanumeric senders.
QString matchKey(const QString &remoteUid);

} // namespace PhoneNumber

#endif // PHONENUMBER_H
