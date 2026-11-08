// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "xmlrepairdevice.h"

#include <QBuffer>
#include <cstring>

namespace {

const qint64 ChunkSize = 64 * 1024;
const char Utf8Replacement[] = "\xEF\xBF\xBD";   // U+FFFD
const char RefReplacement[] = "&#65533;";

bool isValidXmlChar(quint32 cp)
{
    return cp == 0x9 || cp == 0xA || cp == 0xD
            || (cp >= 0x20 && cp <= 0xD7FF)
            || (cp >= 0xE000 && cp <= 0xFFFD)
            || (cp >= 0x10000 && cp <= 0x10FFFF);
}

bool isHighSurrogate(quint32 cp) { return cp >= 0xD800 && cp <= 0xDBFF; }
bool isLowSurrogate(quint32 cp) { return cp >= 0xDC00 && cp <= 0xDFFF; }

enum ParseResult { NotAReference, Incomplete, Reference };

// Parses a character reference ("&#123;" or "&#x7B;") at pos, where data[pos] == '&'.
// Values beyond U+10FFFF are reported as 0x110000, which is not a valid character.
ParseResult parseCharRef(const char *data, int size, int pos, quint32 *codePoint, int *length)
{
    int i = pos + 1;
    if (i >= size)
        return Incomplete;
    if (data[i] != '#')
        return NotAReference;
    if (++i >= size)
        return Incomplete;
    const bool hex = data[i] == 'x' || data[i] == 'X';
    if (hex)
        ++i;
    const int maxDigits = hex ? 8 : 10;
    quint64 value = 0;
    int digits = 0;
    for (; i < size; ++i) {
        const char c = data[i];
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (hex && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (hex && c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else if (c == ';' && digits > 0) {
            *codePoint = value > 0x10FFFF ? 0x110000 : quint32(value);
            *length = i - pos + 1;
            return Reference;
        } else
            return NotAReference;
        if (++digits > maxDigits)
            return NotAReference;
        value = value * (hex ? 16 : 10) + quint64(digit);
    }
    return Incomplete;
}

// Length of the well-formed UTF-8 sequence at pos (RFC 3629), 0 if it is malformed,
// -1 if it is cut off by the end of the buffer.
int utf8SequenceLength(const uchar *data, int size, int pos)
{
    const uchar c = data[pos];
    int length;
    uchar min2 = 0x80;
    uchar max2 = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) {
        length = 2;
    } else if (c == 0xE0) {
        length = 3;
        min2 = 0xA0;
    } else if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) {
        length = 3;
    } else if (c == 0xED) {
        length = 3;
        max2 = 0x9F;            // excludes the UTF-16 surrogates
    } else if (c == 0xF0) {
        length = 4;
        min2 = 0x90;
    } else if (c >= 0xF1 && c <= 0xF3) {
        length = 4;
    } else if (c == 0xF4) {
        length = 4;
        max2 = 0x8F;
    } else {
        return 0;
    }
    for (int k = 1; k < length; ++k) {
        if (pos + k >= size)
            return -1;
        const uchar b = data[pos + k];
        if (b < (k == 1 ? min2 : 0x80) || b > (k == 1 ? max2 : 0xBF))
            return 0;
    }
    return length;
}

// Matches a CESU-8 surrogate pair (ED A0..AF xx ED B0..BF xx) at pos. Returns 1 and the
// combined code point, 0 if it is no pair, -1 if the buffer ends before that is clear.
int cesuPair(const uchar *data, int size, int pos, quint32 *codePoint)
{
    static const uchar min[6] = { 0xED, 0xA0, 0x80, 0xED, 0xB0, 0x80 };
    static const uchar max[6] = { 0xED, 0xAF, 0xBF, 0xED, 0xBF, 0xBF };
    for (int k = 0; k < 6; ++k) {
        if (pos + k >= size)
            return -1;
        const uchar b = data[pos + k];
        if (b < min[k] || b > max[k])
            return 0;
    }
    const quint32 high = 0xD000 | ((data[pos + 1] & 0x3F) << 6) | (data[pos + 2] & 0x3F);
    const quint32 low = 0xD000 | ((data[pos + 4] & 0x3F) << 6) | (data[pos + 5] & 0x3F);
    *codePoint = 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
    return 1;
}

void appendUtf8(QByteArray &out, quint32 cp)
{
    out += char(0xF0 | (cp >> 18));
    out += char(0x80 | ((cp >> 12) & 0x3F));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
}

} // namespace

XmlRepairDevice::XmlRepairDevice(QIODevice *source, QObject *parent)
    : QIODevice(parent)
    , m_source(source)
{
    open(QIODevice::ReadOnly);
}

bool XmlRepairDevice::isSequential() const
{
    return true;
}

bool XmlRepairDevice::atEnd() const
{
    return m_sourceAtEnd && m_pending.isEmpty() && m_outputPos >= m_output.size()
            && QIODevice::bytesAvailable() == 0;
}

qint64 XmlRepairDevice::bytesAvailable() const
{
    return (m_output.size() - m_outputPos) + QIODevice::bytesAvailable();
}

void XmlRepairDevice::close()
{
    m_pending.clear();
    m_output.clear();
    m_outputPos = 0;
    QIODevice::close();
}

int XmlRepairDevice::repairCount() const
{
    return m_repairs;
}

QByteArray XmlRepairDevice::repair(const QByteArray &input, int *repairs)
{
    QByteArray copy(input);
    QBuffer buffer(&copy);
    buffer.open(QIODevice::ReadOnly);
    XmlRepairDevice device(&buffer);
    const QByteArray result = device.readAll();
    if (repairs)
        *repairs = device.repairCount();
    return result;
}

qint64 XmlRepairDevice::readData(char *data, qint64 maxSize)
{
    while (m_output.size() - m_outputPos < maxSize && fill()) {
    }
    const qint64 available = m_output.size() - m_outputPos;
    if (available == 0)
        return m_sourceFailed ? -1 : 0;
    const qint64 count = qMin(maxSize, available);
    std::memcpy(data, m_output.constData() + m_outputPos, size_t(count));
    m_outputPos += int(count);
    if (m_outputPos == m_output.size()) {
        m_output.clear();
        m_outputPos = 0;
    } else if (m_outputPos > 1024 * 1024) {
        m_output.remove(0, m_outputPos);
        m_outputPos = 0;
    }
    return count;
}

qint64 XmlRepairDevice::writeData(const char *, qint64)
{
    return -1;
}

// Reads the next chunk from the source and processes it. Returns false when nothing more
// can be produced.
bool XmlRepairDevice::fill()
{
    if (m_sourceAtEnd)
        return false;
    QByteArray chunk(int(ChunkSize), Qt::Uninitialized);
    const qint64 count = m_source->read(chunk.data(), ChunkSize);
    if (count <= 0) {
        if (count < 0) {
            m_sourceFailed = true;
            setErrorString(m_source->errorString());
        }
        m_sourceAtEnd = true;
        process(true);
        return true;
    }
    chunk.truncate(int(count));
    m_pending.append(chunk);
    process(false);
    return true;
}

void XmlRepairDevice::process(bool final)
{
    const char *data = m_pending.constData();
    const uchar *bytes = reinterpret_cast<const uchar *>(data);
    const int size = m_pending.size();
    QByteArray out;
    out.reserve(size + 16);
    int runStart = 0;   // unchanged bytes are copied in runs
    int i = 0;

    auto replace = [&](int end, const char *with, int next) {
        if (end > runStart)
            out.append(data + runStart, end - runStart);
        if (with)
            out.append(with);
        i = next;
        runStart = next;
        ++m_repairs;
    };

    while (i < size) {
        const uchar c = bytes[i];
        if (c >= 0x20 && c < 0x80 && c != '&') {
            ++i;
            continue;
        }
        if (c == '&') {
            quint32 cp = 0;
            int length = 0;
            const ParseResult result = parseCharRef(data, size, i, &cp, &length);
            if (result == Incomplete && !final)
                break;
            if (result != Reference) {
                ++i;            // an entity like &amp; or a stray '&': left to the parser
                continue;
            }
            if (isHighSurrogate(cp)) {
                const int next = i + length;
                quint32 low = 0;
                int lowLength = 0;
                ParseResult lowResult = NotAReference;
                if (next >= size)
                    lowResult = Incomplete;
                else if (data[next] == '&')
                    lowResult = parseCharRef(data, size, next, &low, &lowLength);
                if (lowResult == Incomplete && !final)
                    break;
                if (lowResult == Reference && isLowSurrogate(low)) {
                    const quint32 full = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    const QByteArray ref = "&#" + QByteArray::number(full) + ';';
                    replace(i, ref.constData(), next + lowLength);
                } else {
                    replace(i, RefReplacement, next);
                }
                continue;
            }
            if (isLowSurrogate(cp)) {
                replace(i, RefReplacement, i + length);
                continue;
            }
            if (!isValidXmlChar(cp)) {
                replace(i, nullptr, i + length);
                continue;
            }
            i += length;
            continue;
        }
        if (c < 0x20) {
            if (c == '\t' || c == '\n' || c == '\r')
                ++i;
            else
                replace(i, nullptr, i + 1);
            continue;
        }
        const int length = utf8SequenceLength(bytes, size, i);
        if (length == -1 && !final)
            break;
        if (length > 0) {
            if (length == 3 && c == 0xEF && bytes[i + 1] == 0xBF
                    && (bytes[i + 2] == 0xBE || bytes[i + 2] == 0xBF)) {
                replace(i, Utf8Replacement, i + 3);     // U+FFFE and U+FFFF are no XML characters
            } else {
                i += length;
            }
            continue;
        }
        if (c == 0xED) {
            quint32 cp = 0;
            const int pair = cesuPair(bytes, size, i, &cp);
            if (pair == -1 && !final)
                break;
            if (pair == 1) {
                QByteArray utf8;
                appendUtf8(utf8, cp);
                replace(i, utf8.constData(), i + 6);
                continue;
            }
        }
        replace(i, Utf8Replacement, i + 1);
    }
    if (i > runStart)
        out.append(data + runStart, i - runStart);
    m_pending.remove(0, i);
    if (final && !m_pending.isEmpty()) {
        // Only reachable for a tail that could not be classified; pass it on unchanged.
        out.append(m_pending);
        m_pending.clear();
    }
    m_output.append(out);
}
