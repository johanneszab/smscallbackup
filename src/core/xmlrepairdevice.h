// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef XMLREPAIRDEVICE_H
#define XMLREPAIRDEVICE_H

#include <QByteArray>
#include <QIODevice>

// A read-only stream that makes the bytes of an SMS Backup & Restore file acceptable to a
// strict XML 1.0 parser such as QXmlStreamReader, while it is being read:
//
//  * The Android app writes characters outside the Basic Multilingual Plane (most emoji)
//    as two UTF-16 surrogate references, e.g. "&#55357;&#56832;" for U+1F600. XML only
//    allows a reference to the whole character, so such pairs are joined into one
//    ("&#128512;"). A lone surrogate (half an emoji) becomes U+FFFD.
//  * References to characters that XML forbids (control codes such as "&#1;") and raw
//    control bytes are dropped.
//  * Invalid UTF-8 becomes U+FFFD; CESU-8 encoded surrogate pairs are joined.
//
// Everything else passes through unchanged, so valid files are not altered at all.
class XmlRepairDevice : public QIODevice
{
public:
    explicit XmlRepairDevice(QIODevice *source, QObject *parent = nullptr);

    bool isSequential() const override;
    bool atEnd() const override;
    qint64 bytesAvailable() const override;
    void close() override;

    // Number of characters or references that were changed or dropped so far.
    int repairCount() const;

    // Repairs a complete buffer (for tests and short strings).
    static QByteArray repair(const QByteArray &input, int *repairs = nullptr);

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 size) override;

private:
    bool fill();
    void process(bool final);

    QIODevice *m_source;
    QByteArray m_pending;        // read from the source, not processed yet
    QByteArray m_output;         // processed, not handed out yet
    int m_outputPos = 0;
    bool m_sourceAtEnd = false;
    bool m_sourceFailed = false;
    int m_repairs = 0;
};

#endif // XMLREPAIRDEVICE_H
