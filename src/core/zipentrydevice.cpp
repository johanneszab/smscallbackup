// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "zipentrydevice.h"

#include <QByteArray>
#include <QCoreApplication>

#include <climits>
#include <zlib.h>

struct ZipInflater
{
    z_stream stream;
    bool initialized = false;
    QByteArray buffer;
};

namespace {

const quint32 LocalHeaderSignature = 0x04034b50;
const quint32 CentralHeaderSignature = 0x02014b50;
const quint32 EndOfCentralDirSignature = 0x06054b50;
const quint32 Zip64EndOfCentralDirSignature = 0x06064b50;
const quint32 Zip64LocatorSignature = 0x07064b50;
const int InputChunk = 64 * 1024;

quint16 le16(const QByteArray &data, int pos)
{
    const uchar *p = reinterpret_cast<const uchar *>(data.constData()) + pos;
    return quint16(p[0] | (p[1] << 8));
}

quint32 le32(const QByteArray &data, int pos)
{
    const uchar *p = reinterpret_cast<const uchar *>(data.constData()) + pos;
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint64 le64(const QByteArray &data, int pos)
{
    return quint64(le32(data, pos)) | (quint64(le32(data, pos + 4)) << 32);
}

} // namespace

ZipEntryDevice::ZipEntryDevice(const QString &zipPath, QObject *parent)
    : QIODevice(parent)
    , m_file(zipPath)
    , m_inflater(new ZipInflater)
{
}

ZipEntryDevice::~ZipEntryDevice()
{
    close();
}

bool ZipEntryDevice::isZipFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray magic = file.read(4);
    return magic == QByteArray("PK\x03\x04", 4) || magic == QByteArray("PK\x05\x06", 4);
}

bool ZipEntryDevice::open(OpenMode mode)
{
    if ((mode & QIODevice::WriteOnly) || isOpen())
        return false;
    if (!m_file.open(QIODevice::ReadOnly)) {
        setErrorString(m_file.errorString());
        return false;
    }
    if (!locateEntry()) {
        const QString error = errorString();
        m_file.close();
        setErrorString(error);
        return false;
    }
    if (m_method == 8) {
        z_stream &z = m_inflater->stream;
        z = z_stream();
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
            m_file.close();
            setErrorString(QCoreApplication::translate("ZipEntryDevice", "Cannot initialize decompression."));
            return false;
        }
        m_inflater->initialized = true;
        m_inflater->buffer.resize(InputChunk);
    }
    m_crc = crc32(0L, Z_NULL, 0);
    m_compressedRead = 0;
    m_produced = 0;
    m_finished = false;
    return QIODevice::open(mode | QIODevice::Unbuffered);
}

void ZipEntryDevice::close()
{
    if (m_inflater && m_inflater->initialized) {
        inflateEnd(&m_inflater->stream);
        m_inflater->initialized = false;
    }
    m_file.close();
    if (isOpen())
        QIODevice::close();
}

bool ZipEntryDevice::isSequential() const
{
    return true;
}

bool ZipEntryDevice::atEnd() const
{
    return m_finished && QIODevice::bytesAvailable() == 0;
}

QString ZipEntryDevice::entryName() const
{
    return m_entryName;
}

qint64 ZipEntryDevice::uncompressedSize() const
{
    return m_uncompressedSize;
}

qint64 ZipEntryDevice::archivePosition() const
{
    return m_dataOffset + m_compressedRead;
}

qint64 ZipEntryDevice::archiveSize() const
{
    return m_file.size();
}

bool ZipEntryDevice::fail(const QString &message)
{
    setErrorString(message);
    return false;
}

// Finds the entry through the central directory, which also has the right sizes for
// entries written with a trailing data descriptor.
bool ZipEntryDevice::locateEntry()
{
    const qint64 fileSize = m_file.size();
    const qint64 tailSize = qMin<qint64>(fileSize, 22 + 65535);
    if (tailSize < 22 || !m_file.seek(fileSize - tailSize))
        return fail(QCoreApplication::translate("ZipEntryDevice", "The file is not a zip archive."));
    const QByteArray tail = m_file.read(tailSize);
    int eocd = -1;
    for (int pos = tail.size() - 22; pos >= 0; --pos) {
        if (le32(tail, pos) == EndOfCentralDirSignature) {
            eocd = pos;
            break;
        }
    }
    if (eocd < 0)
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (no central directory)."));

    quint64 entries = le16(tail, eocd + 10);
    quint64 cdSize = le32(tail, eocd + 12);
    quint64 cdOffset = le32(tail, eocd + 16);
    if (entries == 0xFFFF || cdSize == 0xFFFFFFFF || cdOffset == 0xFFFFFFFF) {
        // ZIP64: the locator sits right before the end record
        if (eocd < 20 || le32(tail, eocd - 20) != Zip64LocatorSignature)
            return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (ZIP64 locator missing)."));
        const quint64 zip64Offset = le64(tail, eocd - 20 + 8);
        if (!m_file.seek(qint64(zip64Offset)))
            return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged."));
        const QByteArray record = m_file.read(56);
        if (record.size() < 56 || le32(record, 0) != Zip64EndOfCentralDirSignature)
            return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (ZIP64 record missing)."));
        entries = le64(record, 32);
        cdSize = le64(record, 40);
        cdOffset = le64(record, 48);
    }
    if (entries == 0)
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is empty."));
    if (cdOffset + cdSize > quint64(fileSize) || cdSize > 64 * 1024 * 1024)
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (bad central directory)."));
    if (!m_file.seek(qint64(cdOffset)))
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged."));
    const QByteArray cd = m_file.read(qint64(cdSize));
    if (quint64(cd.size()) != cdSize)
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (truncated)."));

    struct Entry {
        QString name;
        quint16 flags = 0;
        quint16 method = 0;
        quint32 crc = 0;
        quint64 compressed = 0;
        quint64 uncompressed = 0;
        quint64 localOffset = 0;
    };
    bool haveFirst = false;
    bool haveXml = false;
    Entry first;
    Entry xml;
    int pos = 0;
    for (quint64 n = 0; n < entries; ++n) {
        if (pos + 46 > cd.size() || le32(cd, pos) != CentralHeaderSignature)
            return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (bad directory entry)."));
        Entry entry;
        entry.flags = le16(cd, pos + 8);
        entry.method = le16(cd, pos + 10);
        entry.crc = le32(cd, pos + 16);
        entry.compressed = le32(cd, pos + 20);
        entry.uncompressed = le32(cd, pos + 24);
        const int nameLength = le16(cd, pos + 28);
        const int extraLength = le16(cd, pos + 30);
        const int commentLength = le16(cd, pos + 32);
        entry.localOffset = le32(cd, pos + 42);
        if (pos + 46 + nameLength + extraLength + commentLength > cd.size())
            return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (bad directory entry)."));
        const QByteArray rawName = cd.mid(pos + 46, nameLength);
        entry.name = (entry.flags & 0x800) ? QString::fromUtf8(rawName) : QString::fromLatin1(rawName);
        // ZIP64 extended information: only the fields that are 0xFFFFFFFF above, in order
        int extra = pos + 46 + nameLength;
        const int extraEnd = extra + extraLength;
        while (extra + 4 <= extraEnd) {
            const quint16 id = le16(cd, extra);
            const quint16 size = le16(cd, extra + 2);
            if (extra + 4 + size > extraEnd)
                break;
            if (id == 0x0001) {
                int field = extra + 4;
                const int fieldEnd = field + size;
                if (entry.uncompressed == 0xFFFFFFFF && field + 8 <= fieldEnd) {
                    entry.uncompressed = le64(cd, field);
                    field += 8;
                }
                if (entry.compressed == 0xFFFFFFFF && field + 8 <= fieldEnd) {
                    entry.compressed = le64(cd, field);
                    field += 8;
                }
                if (entry.localOffset == 0xFFFFFFFF && field + 8 <= fieldEnd)
                    entry.localOffset = le64(cd, field);
            }
            extra += 4 + size;
        }
        pos += 46 + nameLength + extraLength + commentLength;

        if (entry.name.endsWith(QLatin1Char('/')))
            continue;   // a directory
        if (!haveFirst) {
            first = entry;
            haveFirst = true;
        }
        if (!haveXml && entry.name.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)) {
            xml = entry;
            haveXml = true;
        }
    }
    if (!haveFirst)
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive contains no file."));
    const Entry &entry = haveXml ? xml : first;

    if (entry.flags & 0x1)
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is password-protected. Unzip it on a computer "
                       "and restore the .xml file inside."));
    if (entry.method != 0 && entry.method != 8)
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive uses an unsupported compression method (%1).").arg(entry.method));

    if (!m_file.seek(qint64(entry.localOffset)))
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged."));
    const QByteArray local = m_file.read(30);
    if (local.size() < 30 || le32(local, 0) != LocalHeaderSignature)
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (bad file header)."));
    m_dataOffset = qint64(entry.localOffset) + 30 + le16(local, 26) + le16(local, 28);
    if (m_dataOffset + qint64(entry.compressed) > fileSize)
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is truncated."));
    if (!m_file.seek(m_dataOffset))
        return fail(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged."));

    m_entryName = entry.name;
    m_method = entry.method;
    m_expectedCrc = entry.crc;
    m_compressedSize = qint64(entry.compressed);
    m_uncompressedSize = qint64(entry.uncompressed);
    return true;
}

qint64 ZipEntryDevice::readData(char *data, qint64 maxSize)
{
    if (m_finished || maxSize <= 0)
        return 0;

    qint64 produced = 0;
    if (m_method == 0) {
        const qint64 wanted = qMin(maxSize, m_compressedSize - m_compressedRead);
        if (wanted > 0) {
            produced = m_file.read(data, wanted);
            if (produced <= 0) {
                setErrorString(QCoreApplication::translate("ZipEntryDevice", "The zip archive is truncated."));
                return -1;
            }
            m_compressedRead += produced;
        }
        if (m_compressedRead >= m_compressedSize)
            m_finished = true;
    } else {
        z_stream &z = m_inflater->stream;
        z.next_out = reinterpret_cast<Bytef *>(data);
        z.avail_out = uInt(qMin<qint64>(maxSize, INT_MAX));
        while (z.avail_out > 0 && !m_finished) {
            if (z.avail_in == 0) {
                const qint64 wanted = qMin<qint64>(InputChunk, m_compressedSize - m_compressedRead);
                if (wanted <= 0) {
                    setErrorString(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (data ends early)."));
                    return -1;
                }
                const qint64 count = m_file.read(m_inflater->buffer.data(), wanted);
                if (count <= 0) {
                    setErrorString(QCoreApplication::translate("ZipEntryDevice", "The zip archive is truncated."));
                    return -1;
                }
                m_compressedRead += count;
                z.next_in = reinterpret_cast<Bytef *>(m_inflater->buffer.data());
                z.avail_in = uInt(count);
            }
            const int result = inflate(&z, Z_NO_FLUSH);
            if (result == Z_STREAM_END) {
                m_finished = true;
            } else if (result != Z_OK) {
                setErrorString(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (%1).")
                               .arg(QString::fromLatin1(z.msg ? z.msg : "inflate error")));
                return -1;
            }
        }
        produced = qMin<qint64>(maxSize, INT_MAX) - qint64(z.avail_out);
    }

    if (produced > 0) {
        m_crc = crc32(m_crc, reinterpret_cast<const Bytef *>(data), uInt(produced));
        m_produced += produced;
    }
    if (m_finished && m_crc != m_expectedCrc) {
        setErrorString(QCoreApplication::translate("ZipEntryDevice", "The zip archive is damaged (checksum mismatch)."));
        return -1;
    }
    return produced;
}

qint64 ZipEntryDevice::writeData(const char *, qint64)
{
    return -1;
}
