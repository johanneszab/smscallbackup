// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef ZIPENTRYDEVICE_H
#define ZIPENTRYDEVICE_H

#include <QFile>
#include <QIODevice>
#include <QScopedPointer>

struct ZipInflater;

// Streams one file out of a zip archive without unpacking it, for the compressed backups
// (.xml.zip) of SMS Backup & Restore: the first entry whose name ends in ".xml", or the
// first file of the archive. Stored and deflated entries and ZIP64 archives are supported;
// password-protected ones are refused with a clear error.
class ZipEntryDevice : public QIODevice
{
public:
    explicit ZipEntryDevice(const QString &zipPath, QObject *parent = nullptr);
    ~ZipEntryDevice() override;

    // Opens the archive and finds the entry. On failure errorString() says why.
    bool open(OpenMode mode) override;
    void close() override;
    bool isSequential() const override;
    bool atEnd() const override;

    QString entryName() const;
    qint64 uncompressedSize() const;
    // Position in the zip file, for progress reporting.
    qint64 archivePosition() const;
    qint64 archiveSize() const;

    // True if the file starts like a zip archive.
    static bool isZipFile(const QString &path);

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 size) override;

private:
    bool locateEntry();
    bool fail(const QString &message);

    QFile m_file;
    QScopedPointer<ZipInflater> m_inflater;
    QString m_entryName;
    int m_method = 0;
    quint32 m_expectedCrc = 0;
    quint32 m_crc = 0;
    qint64 m_dataOffset = 0;
    qint64 m_compressedSize = 0;
    qint64 m_uncompressedSize = 0;
    qint64 m_compressedRead = 0;
    qint64 m_produced = 0;
    bool m_finished = false;
};

#endif // ZIPENTRYDEVICE_H
