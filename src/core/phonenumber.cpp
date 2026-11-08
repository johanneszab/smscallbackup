// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "phonenumber.h"

namespace PhoneNumber {

QString minimize(const QString &input, int maxDigits)
{
    static const QString separators = QStringLiteral(" .-()[]");
    static const QString dtmf = QStringLiteral("pPwWxX,;#*");

    // SIP addresses compare by their user part
    QString number = input;
    if (number.startsWith(QLatin1String("sips:")) || number.startsWith(QLatin1Char(':'))) {
        const int colon = number.indexOf(QLatin1Char(':'));
        const int at = number.indexOf(QLatin1Char('@'), colon + 1);
        if (at != -1)
            number = number.mid(colon + 1, at - colon - 1);
    }

    QString result;
    result.reserve(number.size());
    bool haveStart = false;
    bool haveDigit = false;
    int dtmfStart = -1;
    for (const QChar c : number) {
        if (c.isDigit()) {
            result += QChar(QLatin1Char(char('0' + c.digitValue())));
            haveStart = true;
            haveDigit = true;
        } else if (c == QLatin1Char('+')) {
            if (!haveStart) {
                result += c;
                haveStart = true;
            } else if (dtmfStart != -1) {
                result += c;
            }
        } else if (separators.contains(c)) {
            continue;
        } else if (dtmf.contains(c)) {
            if (c.isLetter() && !haveDigit)
                continue;       // letters count as DTMF only after some digits
            if (dtmfStart == -1)
                dtmfStart = result.size();
            if (c == QLatin1Char('x') || c == QLatin1Char('X') || c == QLatin1Char(','))
                result += QLatin1Char('p');
            else if (c == QLatin1Char(';'))
                result += QLatin1Char('w');
            else
                result += c;
        }
    }

    if (maxDigits > 0) {
        int count = 0;
        // As in qtcontacts-sqlite, the very first character is never counted.
        for (int i = (dtmfStart == -1 ? result.size() : dtmfStart) - 1; i > 0; --i) {
            const QChar c = result.at(i);
            if ((c.isDigit() || c == QLatin1Char('+')) && ++count == maxDigits) {
                result = result.mid(i);
                break;
            }
        }
    }
    return result.trimmed();
}

QString matchKey(const QString &remoteUid)
{
    const QString minimized = minimize(remoteUid).toLower();
    return minimized.isEmpty() ? remoteUid.toLower() : minimized;
}

} // namespace PhoneNumber
