// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "mapping.h"
#include "phonenumber.h"
#include "schedule.h"
#include "sbrreader.h"
#include "sbrwriter.h"
#include "xmlrepairdevice.h"
#include "zipentrydevice.h"

#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>

namespace {

QString dataFile(const char *name)
{
    return QStringLiteral(TESTDATA_DIR "/") + QLatin1String(name);
}

const QString Emoji = QString::fromUtf8("\xF0\x9F\x98\x80");   // U+1F600

struct Collected
{
    QList<Sbr::SmsRecord> sms;
    QList<Sbr::MmsRecord> mms;
    QList<Sbr::CallRecord> calls;
};

bool readAll(const QString &path, Collected *out, QString *error = nullptr, SbrReader::Info *info = nullptr)
{
    SbrReader reader(path);
    SbrReader::Callbacks callbacks;
    callbacks.sms = [out](const Sbr::SmsRecord &r) { out->sms.append(r); return true; };
    callbacks.mms = [out](const Sbr::MmsRecord &r) { out->mms.append(r); return true; };
    callbacks.call = [out](const Sbr::CallRecord &r) { out->calls.append(r); return true; };
    const bool ok = reader.read(callbacks);
    if (error)
        *error = reader.errorString();
    if (info)
        *info = reader.info();
    return ok;
}

// Reads a device in small, odd-sized steps to exercise the buffering.
QByteArray readInSteps(QIODevice *device, int step)
{
    QByteArray result;
    for (;;) {
        const QByteArray part = device->read(step);
        if (part.isEmpty())
            break;
        result += part;
    }
    return result;
}

} // namespace

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void repairSurrogates_data();
    void repairSurrogates();
    void repairAcrossChunks();
    void repairedSampleParses();

    void minimizePhoneNumbers_data();
    void minimizePhoneNumbers();

    void readSms();
    void readCalls();
    void readZip();
    void readEncryptedZip();
    void readNotABackup();
    void probe();
    void cancelReading();

    void escape();
    void writerFormat();
    void writerRoundTrip();

    void smsToEvent();
    void callToEvent();
    void eventToRecordsRoundTrip();
    void duplicateKeys();
    void simSlots();

    void scheduleDaily();
    void scheduleWeeklyMonthly();
    void scheduleDue();
    void scheduleDaylightSaving();

    void mmsAndRcsToEvents();
    void mmsGroups();
    void eventToMmsRoundTrip();
    void archiveKeys();
    void archiveCopy();
    void partFileNames();
};

void TestCore::repairSurrogates_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<QByteArray>("expected");
    QTest::addColumn<int>("repairs");

    QTest::newRow("valid file unchanged") << QByteArray("<a b=\"x&amp;y&#10;&#x1F600;\"/>")
                                          << QByteArray("<a b=\"x&amp;y&#10;&#x1F600;\"/>") << 0;
    QTest::newRow("surrogate pair") << QByteArray("a&#55357;&#56832;b") << QByteArray("a&#128512;b") << 1;
    QTest::newRow("hex surrogate pair") << QByteArray("&#xD83D;&#xDE00;") << QByteArray("&#128512;") << 1;
    QTest::newRow("lone high surrogate") << QByteArray("a&#55357;b") << QByteArray("a&#65533;b") << 1;
    QTest::newRow("high surrogate at end") << QByteArray("a&#55357;") << QByteArray("a&#65533;") << 1;
    QTest::newRow("lone low surrogate") << QByteArray("&#56832;") << QByteArray("&#65533;") << 1;
    QTest::newRow("control reference") << QByteArray("a&#1;b&#x1f;c") << QByteArray("abc") << 2;
    QTest::newRow("allowed controls") << QByteArray("&#9;&#10;&#13;\t\n\r") << QByteArray("&#9;&#10;&#13;\t\n\r") << 0;
    QTest::newRow("raw control byte") << QByteArray("a\x01" "b\x7f") << QByteArray("ab\x7f") << 1;
    QTest::newRow("out of range reference") << QByteArray("a&#1114112;b") << QByteArray("ab") << 1;
    QTest::newRow("no reference") << QByteArray("&#;&#x;&# 1;&lt;") << QByteArray("&#;&#x;&# 1;&lt;") << 0;
    QTest::newRow("invalid utf-8") << QByteArray("a\xC3(b") << QByteArray("a\xEF\xBF\xBD(b") << 1;
    QTest::newRow("truncated utf-8 at end") << QByteArray("a\xE2\x82") << QByteArray("a\xEF\xBF\xBD\xEF\xBF\xBD") << 2;
    QTest::newRow("valid utf-8") << QByteArray("\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80") << QByteArray("\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80") << 0;
    QTest::newRow("cesu-8 pair") << QByteArray("\xED\xA0\xBD\xED\xB8\x80") << QByteArray("\xF0\x9F\x98\x80") << 1;
    QTest::newRow("non-character") << QByteArray("\xEF\xBF\xBE") << QByteArray("\xEF\xBF\xBD") << 1;
}

void TestCore::repairSurrogates()
{
    QFETCH(QByteArray, input);
    QFETCH(QByteArray, expected);
    QFETCH(int, repairs);
    int count = -1;
    QCOMPARE(XmlRepairDevice::repair(input, &count), expected);
    QCOMPARE(count, repairs);
}

void TestCore::repairAcrossChunks()
{
    // The device reads its source in 64 KiB chunks; put every construct across that border.
    const int border = 64 * 1024;
    struct Case { QByteArray piece; QByteArray expected; };
    const Case cases[] = {
        { QByteArray("&#55357;&#56832;"), QByteArray("&#128512;") },
        { QByteArray("&#1;"), QByteArray() },
        { QByteArray("\xF0\x9F\x98\x80"), QByteArray("\xF0\x9F\x98\x80") },
        { QByteArray("\xED\xA0\xBD\xED\xB8\x80"), QByteArray("\xF0\x9F\x98\x80") },
    };
    for (const Case &c : cases) {
        for (int offset = 1; offset < c.piece.size() + 3; ++offset) {
            QByteArray input(border - offset, 'a');
            input += c.piece;
            input += "z";
            QByteArray expected(border - offset, 'a');
            expected += c.expected;
            expected += "z";
            QBuffer buffer(&input);
            buffer.open(QIODevice::ReadOnly);
            XmlRepairDevice device(&buffer);
            QCOMPARE(readInSteps(&device, 777), expected);
            QVERIFY(device.atEnd());
        }
    }
}

void TestCore::repairedSampleParses()
{
    QFile file(dataFile("sms-sample.xml"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray raw = file.readAll();

    // A strict parser rejects the file as the Android app writes it ...
    QXmlStreamReader strict(raw);
    while (!strict.atEnd())
        strict.readNext();
    QVERIFY(strict.hasError());

    // ... and accepts it after the repair.
    QXmlStreamReader repaired(XmlRepairDevice::repair(raw));
    QString firstBody;
    while (!repaired.atEnd()) {
        if (repaired.readNext() == QXmlStreamReader::StartElement && repaired.name() == QLatin1String("sms")
                && firstBody.isNull())
            firstBody = repaired.attributes().value(QLatin1String("body")).toString();
    }
    QVERIFY2(!repaired.hasError(), qPrintable(repaired.errorString()));
    QCOMPARE(firstBody, QStringLiteral("Hello ") + Emoji + QStringLiteral(" how are you?\nSecond line"));
}

void TestCore::minimizePhoneNumbers_data()
{
    QTest::addColumn<QString>("number");
    QTest::addColumn<QString>("minimized");
    QTest::addColumn<QString>("key");

    QTest::newRow("international") << "+49 176 1234-5678" << "12345678" << "12345678";
    QTest::newRow("national") << "0176 12345678" << "12345678" << "12345678";
    QTest::newRow("punctuation") << "(202) 555-0101" << "25550101" << "25550101";
    QTest::newRow("short") << "112" << "112" << "112";
    QTest::newRow("sender name") << "ACME" << "" << "acme";
    QTest::newRow("dtmf") << "+12025550105,123" << "25550105p123" << "25550105p123";
    QTest::newRow("unicode digits") << QString::fromUtf8("\xD9\xA1\xD9\xA2\xD9\xA3") << "123" << "123";
    QTest::newRow("empty") << "" << "" << "";
}

void TestCore::minimizePhoneNumbers()
{
    QFETCH(QString, number);
    QFETCH(QString, minimized);
    QFETCH(QString, key);
    QCOMPARE(PhoneNumber::minimize(number), minimized);
    QCOMPARE(PhoneNumber::matchKey(number), key);
}

void TestCore::readSms()
{
    Collected c;
    QString error;
    SbrReader::Info info;
    QVERIFY2(readAll(dataFile("sms-sample.xml"), &c, &error, &info), qPrintable(error));
    QCOMPARE(info.kind, SbrReader::MessagesKind);
    QCOMPARE(info.count, 10);
    QCOMPARE(info.backupDate, Q_INT64_C(1706781600000));
    QCOMPARE(info.backupSet, QStringLiteral("11111111-2222-4333-8444-555555555555"));
    QCOMPARE(c.sms.size(), 9);
    QCOMPARE(c.mms.size(), 1);
    QCOMPARE(c.calls.size(), 0);

    const Sbr::SmsRecord &first = c.sms.at(0);
    QCOMPARE(first.address, QStringLiteral("+12025550101"));
    QCOMPARE(first.date, Q_INT64_C(1706770000123));
    QCOMPARE(first.dateSent, Q_INT64_C(1706769998000));
    QCOMPARE(first.type, int(Sbr::BoxReceived));
    QVERIFY(first.subject.isNull());
    QCOMPARE(first.serviceCenter, QStringLiteral("+12025550000"));
    QCOMPARE(first.body, QStringLiteral("Hello ") + Emoji + QStringLiteral(" how are you?\nSecond line"));
    QCOMPARE(first.subId, QStringLiteral("1"));
    QCOMPARE(first.contactName, QStringLiteral("Alice Example"));

    QCOMPARE(c.sms.at(1).body, QStringLiteral("Fine, thanks! & you?"));
    QCOMPARE(c.sms.at(1).status, int(Sbr::SmsStatusComplete));
    QCOMPARE(c.sms.at(2).read, 0);
    QCOMPARE(c.sms.at(3).body, QStringLiteral("Your parcel arrives today"));   // &#1; dropped
    QCOMPARE(c.sms.at(7).body, QStringLiteral("Apostrophe ' quote \" and <tag>"));
    QVERIFY(c.sms.at(8).body.isNull());     // body="null"

    const Sbr::MmsRecord &mms = c.mms.at(0);
    QCOMPARE(mms.value(QStringLiteral("msg_box")), QStringLiteral("1"));
    QCOMPARE(mms.value(QStringLiteral("m_type")), QStringLiteral("132"));
    QVERIFY(mms.value(QStringLiteral("sub")).isNull());
    QCOMPARE(mms.parts.size(), 3);
    QCOMPARE(mms.parts.at(1).value(QStringLiteral("ct")), QStringLiteral("image/png"));
    QCOMPARE(mms.parts.at(0).value(QStringLiteral("cid")), QStringLiteral("<smil>"));
    QCOMPARE(mms.addresses.size(), 2);
    QCOMPARE(mms.addresses.at(0).type, int(Sbr::AddrFrom));
}

void TestCore::readCalls()
{
    Collected c;
    QString error;
    SbrReader::Info info;
    QVERIFY2(readAll(dataFile("calls-sample.xml"), &c, &error, &info), qPrintable(error));
    QCOMPARE(info.kind, SbrReader::CallsKind);
    QCOMPARE(info.count, 10);
    QCOMPARE(c.calls.size(), 10);
    QCOMPARE(c.sms.size(), 0);
    const Sbr::CallRecord &first = c.calls.at(0);
    QCOMPARE(first.number, QStringLiteral("+12025550101"));
    QCOMPARE(first.duration, Q_INT64_C(62));
    QCOMPARE(first.date, Q_INT64_C(1706760000008));
    QCOMPARE(first.type, int(Sbr::CallIncoming));
    QCOMPARE(first.presentation, int(Sbr::PresentationAllowed));
    QVERIFY(c.calls.at(2).subscriptionId.isNull());
    QCOMPARE(c.calls.at(7).number, QString());
    QCOMPARE(c.calls.at(7).presentation, int(Sbr::PresentationRestricted));
    QCOMPARE(c.calls.at(8).postDialDigits, QStringLiteral(",123"));
}

void TestCore::readZip()
{
    QVERIFY(ZipEntryDevice::isZipFile(dataFile("sms-sample.xml.zip")));
    QVERIFY(!ZipEntryDevice::isZipFile(dataFile("sms-sample.xml")));
    Collected plain;
    Collected zipped;
    QString error;
    SbrReader::Info info;
    QVERIFY(readAll(dataFile("sms-sample.xml"), &plain));
    QVERIFY2(readAll(dataFile("sms-sample.xml.zip"), &zipped, &error, &info), qPrintable(error));
    QVERIFY(info.compressed);
    QCOMPARE(zipped.sms.size(), plain.sms.size());
    QCOMPARE(zipped.mms.size(), plain.mms.size());
    for (int i = 0; i < plain.sms.size(); ++i)
        QCOMPARE(zipped.sms.at(i).body, plain.sms.at(i).body);

    ZipEntryDevice zip(dataFile("sms-sample.xml.zip"));
    QVERIFY(zip.open(QIODevice::ReadOnly));
    QCOMPARE(zip.entryName(), QStringLiteral("sms-sample.xml"));
    QFile file(dataFile("sms-sample.xml"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(readInSteps(&zip, 100), file.readAll());
}

void TestCore::readEncryptedZip()
{
    Collected c;
    QString error;
    QVERIFY(!readAll(dataFile("encrypted.xml.zip"), &c, &error));
    QVERIFY2(error.contains(QLatin1String("password")), qPrintable(error));
}

void TestCore::readNotABackup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("other.xml"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<?xml version='1.0'?><contacts><contact name='x'/></contacts>");
    file.close();
    Collected c;
    QString error;
    QVERIFY(!readAll(path, &c, &error));
    QVERIFY2(error.contains(QLatin1String("not a backup")), qPrintable(error));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("<?xml version='1.0'?><smses count='2'><sms address='1' date='5' type='1' body='x'/><sms ");
    file.close();
    QVERIFY(!readAll(path, &c, &error));
    QVERIFY2(error.contains(QLatin1String("not a valid backup")), qPrintable(error));
    QCOMPARE(c.sms.size(), 1);
}

void TestCore::probe()
{
    const SbrReader::Info sms = SbrReader::probe(dataFile("sms-sample.xml.zip"));
    QVERIFY(sms.ok);
    QVERIFY(sms.compressed);
    QCOMPARE(sms.kind, SbrReader::MessagesKind);
    QCOMPARE(sms.count, 10);
    const SbrReader::Info calls = SbrReader::probe(dataFile("calls-sample.xml"));
    QVERIFY(calls.ok);
    QCOMPARE(calls.kind, SbrReader::CallsKind);
    QCOMPARE(calls.backupDate, Q_INT64_C(1706781600000));
    QVERIFY(!SbrReader::probe(dataFile("missing.xml")).ok);
}

void TestCore::cancelReading()
{
    SbrReader reader(dataFile("calls-sample.xml"));
    int seen = 0;
    SbrReader::Callbacks callbacks;
    callbacks.call = [&seen](const Sbr::CallRecord &) { return ++seen < 3; };
    QVERIFY(!reader.read(callbacks));
    QVERIFY(reader.wasCancelled());
    QCOMPARE(seen, 3);
}

void TestCore::escape()
{
    QString input = QStringLiteral("a&b<c>\"d'e\nf\tg\r");
    input += QChar(0x01);
    input += Emoji;
    input += QChar(0xD83D);     // lone high surrogate
    input += QStringLiteral("x");
    QByteArray expected("a&amp;b&lt;c&gt;&quot;d'e&#10;f&#9;g&#13;\xF0\x9F\x98\x80\xEF\xBF\xBDx");
    QCOMPARE(SbrWriter::escape(input), expected);
    QCOMPARE(SbrWriter::escape(QString()), QByteArray("null"));
    QCOMPARE(SbrWriter::escape(QLatin1String("")), QByteArray(""));
}

void TestCore::writerFormat()
{
    QByteArray out;
    QBuffer buffer(&out);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    SbrWriter writer(&buffer, SbrWriter::MessagesKind);
    QVERIFY(writer.begin(1, QStringLiteral("abc"), Q_INT64_C(1706781600000), QStringLiteral("Test 1.0")));
    Sbr::SmsRecord r;
    r.address = QStringLiteral("+12025550101");
    r.date = Q_INT64_C(1706770000000);
    r.type = Sbr::BoxReceived;
    r.body = QStringLiteral("Hi \"there\"");
    r.read = 1;
    r.status = -1;
    r.dateSent = Q_INT64_C(1706769998000);
    r.subId = QStringLiteral("1");
    r.readableDate = QStringLiteral("Feb 1, 2024 07:46:40");
    r.contactName = QStringLiteral("Alice Example");
    QVERIFY(writer.writeSms(r));
    QVERIFY(writer.end());

    const QList<QByteArray> lines = out.split('\n');
    QCOMPARE(lines.size(), 6);
    QCOMPARE(lines.at(0), QByteArray("<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>"));
    QVERIFY(lines.at(1).startsWith("<!--File Created By Test 1.0 on "));
    QVERIFY(lines.at(1).endsWith("-->"));
    QCOMPARE(lines.at(2), QByteArray("<smses count=\"1\" backup_set=\"abc\" backup_date=\"1706781600000\" type=\"full\">"));
    QCOMPARE(lines.at(3), QByteArray("  <sms protocol=\"0\" address=\"+12025550101\" date=\"1706770000000\" type=\"1\" "
                                     "subject=\"null\" body=\"Hi &quot;there&quot;\" toa=\"null\" sc_toa=\"null\" "
                                     "service_center=\"null\" read=\"1\" status=\"-1\" locked=\"0\" "
                                     "date_sent=\"1706769998000\" sub_id=\"1\" readable_date=\"Feb 1, 2024 07:46:40\" "
                                     "contact_name=\"Alice Example\" />"));
    QCOMPARE(lines.at(4), QByteArray("</smses>"));
    QCOMPARE(lines.at(5), QByteArray());

    QByteArray callOut;
    QBuffer callBuffer(&callOut);
    QVERIFY(callBuffer.open(QIODevice::WriteOnly));
    SbrWriter callWriter(&callBuffer, SbrWriter::CallsKind);
    QVERIFY(callWriter.begin(1, QStringLiteral("abc"), Q_INT64_C(1706781600000), QStringLiteral("Test")));
    Sbr::CallRecord call;
    call.number = QStringLiteral("+12025550101");
    call.duration = 62;
    call.date = Q_INT64_C(1706760000000);
    call.type = Sbr::CallIncoming;
    call.postDialDigits = QLatin1String("");
    call.readableDate = QStringLiteral("Feb 1, 2024 05:00:00");
    call.contactName = QStringLiteral("(Unknown)");
    QVERIFY(callWriter.writeCall(call));
    QVERIFY(callWriter.end());
    QCOMPARE(callOut.split('\n').at(3),
             QByteArray("  <call number=\"+12025550101\" duration=\"62\" date=\"1706760000000\" type=\"1\" "
                        "presentation=\"1\" subscription_id=\"null\" post_dial_digits=\"\" "
                        "subscription_component_name=\"null\" readable_date=\"Feb 1, 2024 05:00:00\" "
                        "contact_name=\"(Unknown)\" />"));
    QVERIFY(callOut.endsWith("</calls>\n"));
}

void TestCore::writerRoundTrip()
{
    Collected original;
    QVERIFY(readAll(dataFile("sms-sample.xml"), &original));
    Collected originalCalls;
    QVERIFY(readAll(dataFile("calls-sample.xml"), &originalCalls));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString smsPath = dir.filePath(QStringLiteral("sms.xml"));
    const QString callsPath = dir.filePath(QStringLiteral("calls.xml"));
    {
        QFile file(smsPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        SbrWriter writer(&file, SbrWriter::MessagesKind);
        QVERIFY(writer.begin(original.sms.size() + original.mms.size(), QStringLiteral("set"), 1, QStringLiteral("Test")));
        for (const Sbr::SmsRecord &r : original.sms)
            QVERIFY(writer.writeSms(r));
        for (const Sbr::MmsRecord &r : original.mms)
            QVERIFY(writer.writeMms(r));
        QVERIFY(writer.end());
    }
    {
        QFile file(callsPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        SbrWriter writer(&file, SbrWriter::CallsKind);
        QVERIFY(writer.begin(originalCalls.calls.size(), QStringLiteral("set"), 1, QStringLiteral("Test")));
        for (const Sbr::CallRecord &r : originalCalls.calls)
            QVERIFY(writer.writeCall(r));
        QVERIFY(writer.end());
    }

    // What we write needs no repair: a strict parser accepts it.
    QFile written(smsPath);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QXmlStreamReader strict(&written);
    while (!strict.atEnd())
        strict.readNext();
    QVERIFY2(!strict.hasError(), qPrintable(strict.errorString()));

    Collected copy;
    QString error;
    QVERIFY2(readAll(smsPath, &copy, &error), qPrintable(error));
    QCOMPARE(copy.sms.size(), original.sms.size());
    for (int i = 0; i < original.sms.size(); ++i) {
        const Sbr::SmsRecord &a = original.sms.at(i);
        const Sbr::SmsRecord &b = copy.sms.at(i);
        QCOMPARE(b.address, a.address);
        QCOMPARE(b.date, a.date);
        QCOMPARE(b.dateSent, a.dateSent);
        QCOMPARE(b.type, a.type);
        QCOMPARE(b.body, a.body);
        QCOMPARE(b.read, a.read);
        QCOMPARE(b.status, a.status);
        QCOMPARE(b.subId, a.subId);
        QCOMPARE(b.serviceCenter, a.serviceCenter);
        QCOMPARE(b.contactName, a.contactName);
        QCOMPARE(b.readableDate, a.readableDate);
    }
    QCOMPARE(copy.mms.size(), 1);
    QCOMPARE(copy.mms.at(0).attributes, original.mms.at(0).attributes);
    QCOMPARE(copy.mms.at(0).parts.size(), 3);
    QCOMPARE(copy.mms.at(0).parts.at(1).attributes, original.mms.at(0).parts.at(1).attributes);
    QCOMPARE(copy.mms.at(0).addresses.at(1).address, QStringLiteral("insert-address-token"));

    Collected callsCopy;
    QVERIFY2(readAll(callsPath, &callsCopy, &error), qPrintable(error));
    QCOMPARE(callsCopy.calls.size(), originalCalls.calls.size());
    for (int i = 0; i < originalCalls.calls.size(); ++i) {
        const Sbr::CallRecord &a = originalCalls.calls.at(i);
        const Sbr::CallRecord &b = callsCopy.calls.at(i);
        QCOMPARE(b.number, a.number);
        QCOMPARE(b.duration, a.duration);
        QCOMPARE(b.date, a.date);
        QCOMPARE(b.type, a.type);
        QCOMPARE(b.presentation, a.presentation);
        QCOMPARE(b.subscriptionId, a.subscriptionId);
        QCOMPARE(b.postDialDigits, a.postDialDigits);
        QCOMPARE(b.subscriptionComponentName, a.subscriptionComponentName);
    }
}

void TestCore::smsToEvent()
{
    Collected c;
    QVERIFY(readAll(dataFile("sms-sample.xml"), &c));
    Mapping::ImportOptions options;
    HistoryEvent e;

    QCOMPARE(Mapping::smsToEvent(c.sms.at(0), options, &e), Mapping::NotSkipped);
    QCOMPARE(e.type, int(HistoryEvent::SmsEvent));
    QCOMPARE(e.direction, int(HistoryEvent::Inbound));
    QCOMPARE(e.endTime, Q_INT64_C(1706770000));
    QCOMPARE(e.startTime, Q_INT64_C(1706769998));    // date_sent
    QCOMPARE(e.remoteUid, QStringLiteral("+12025550101"));
    QCOMPARE(e.status, int(HistoryEvent::UnknownStatus));
    QVERIFY(e.isRead);

    QCOMPARE(Mapping::smsToEvent(c.sms.at(1), options, &e), Mapping::NotSkipped);
    QCOMPARE(e.direction, int(HistoryEvent::Outbound));
    QCOMPARE(e.status, int(HistoryEvent::DeliveredStatus));
    QCOMPARE(e.startTime, e.endTime);

    // unread, imported as read unless asked otherwise
    QCOMPARE(Mapping::smsToEvent(c.sms.at(2), options, &e), Mapping::NotSkipped);
    QVERIFY(e.isRead);
    Mapping::ImportOptions keepUnread;
    keepUnread.markAsRead = false;
    QCOMPARE(Mapping::smsToEvent(c.sms.at(2), keepUnread, &e), Mapping::NotSkipped);
    QVERIFY(!e.isRead);
    QCOMPARE(e.startTime, e.endTime);   // no date_sent

    QCOMPARE(Mapping::smsToEvent(c.sms.at(4), options, &e), Mapping::SkipDraft);
    QCOMPARE(Mapping::smsToEvent(c.sms.at(5), options, &e), Mapping::SkipUnsent);

    QCOMPARE(Mapping::smsToEvent(c.sms.at(6), options, &e), Mapping::NotSkipped);
    QCOMPARE(e.remoteUid, QStringLiteral("Example Bank"));     // RCS business chat

    // date_sent more than a week before the receive time is ignored
    QCOMPARE(Mapping::smsToEvent(c.sms.at(7), options, &e), Mapping::NotSkipped);
    QCOMPARE(e.startTime, e.endTime);

    QCOMPARE(Mapping::smsToEvent(c.sms.at(8), options, &e), Mapping::NotSkipped);
    QCOMPARE(e.freeText, QLatin1String(""));

    Sbr::SmsRecord noAddress = c.sms.at(0);
    noAddress.address = QStringLiteral("  ");
    QCOMPARE(Mapping::smsToEvent(noAddress, options, &e), Mapping::SkipNoAddress);
    Sbr::SmsRecord noDate = c.sms.at(0);
    noDate.date = 0;
    QCOMPARE(Mapping::smsToEvent(noDate, options, &e), Mapping::SkipNoDate);
    Sbr::SmsRecord seconds = c.sms.at(1);
    seconds.date = 1706770060;      // old backups used seconds
    QCOMPARE(Mapping::smsToEvent(seconds, options, &e), Mapping::NotSkipped);
    QCOMPARE(e.endTime, Q_INT64_C(1706770060));
}

void TestCore::callToEvent()
{
    Collected c;
    QVERIFY(readAll(dataFile("calls-sample.xml"), &c));
    const Mapping::ImportOptions options;
    HistoryEvent e;

    QCOMPARE(Mapping::callToEvent(c.calls.at(0), options, &e), Mapping::NotSkipped);
    QCOMPARE(e.type, int(HistoryEvent::CallEvent));
    QCOMPARE(e.direction, int(HistoryEvent::Inbound));
    QCOMPARE(e.startTime, Q_INT64_C(1706760000));
    QCOMPARE(e.endTime, Q_INT64_C(1706760062));
    QVERIFY(!e.isMissedCall);

    QCOMPARE(Mapping::callToEvent(c.calls.at(1), options, &e), Mapping::NotSkipped);
    QCOMPARE(e.direction, int(HistoryEvent::Outbound));
    QCOMPARE(e.callDuration(), Q_INT64_C(5));

    QCOMPARE(Mapping::callToEvent(c.calls.at(2), options, &e), Mapping::NotSkipped);   // missed
    QVERIFY(e.isMissedCall);
    QCOMPARE(e.endTime, e.startTime);
    QVERIFY(e.incomingStatus.isEmpty());

    QCOMPARE(Mapping::callToEvent(c.calls.at(3), options, &e), Mapping::NotSkipped);   // voicemail
    QVERIFY(e.isMissedCall);

    for (int i : { 4, 5 }) {    // rejected, blocked
        QCOMPARE(Mapping::callToEvent(c.calls.at(i), options, &e), Mapping::NotSkipped);
        QVERIFY(e.isMissedCall);
        QCOMPARE(e.incomingStatus, QStringLiteral("rejected"));
    }

    QCOMPARE(Mapping::callToEvent(c.calls.at(6), options, &e), Mapping::NotSkipped);   // answered elsewhere
    QVERIFY(!e.isMissedCall);
    QCOMPARE(e.direction, int(HistoryEvent::Inbound));

    QCOMPARE(Mapping::callToEvent(c.calls.at(7), options, &e), Mapping::NotSkipped);   // private
    QVERIFY(e.remoteUid.isEmpty());
    Sbr::CallRecord oldStyle = c.calls.at(0);
    oldStyle.number = QStringLiteral("-2");
    QCOMPARE(Mapping::callToEvent(oldStyle, options, &e), Mapping::NotSkipped);
    QVERIFY(e.remoteUid.isEmpty());

    QCOMPARE(Mapping::callToEvent(c.calls.at(8), options, &e), Mapping::NotSkipped);   // extension
    QCOMPARE(e.remoteUid, QStringLiteral("+12025550105,123"));

    QCOMPARE(Mapping::callToEvent(c.calls.at(9), options, &e), Mapping::SkipUnknownType);
}

void TestCore::eventToRecordsRoundTrip()
{
    const Mapping::ImportOptions options;
    HistoryEvent in;
    in.type = HistoryEvent::SmsEvent;
    in.direction = HistoryEvent::Inbound;
    in.startTime = 1706769998;
    in.endTime = 1706770000;
    in.localUid = Mapping::ringAccount(2);
    in.remoteUid = QStringLiteral("+12025550101");
    in.freeText = QStringLiteral("Hi ") + Emoji;
    in.isRead = true;

    Sbr::SmsRecord sms = Mapping::eventToSms(in, QStringLiteral("Alice"));
    QCOMPARE(sms.type, int(Sbr::BoxReceived));
    QCOMPARE(sms.date, Q_INT64_C(1706770000000));
    QCOMPARE(sms.dateSent, Q_INT64_C(1706769998000));
    QCOMPARE(sms.subId, QStringLiteral("2"));
    QCOMPARE(sms.contactName, QStringLiteral("Alice"));
    QVERIFY(!sms.readableDate.isEmpty());
    HistoryEvent out;
    QCOMPARE(Mapping::smsToEvent(sms, options, &out), Mapping::NotSkipped);
    QCOMPARE(out.startTime, in.startTime);
    QCOMPARE(out.endTime, in.endTime);
    QCOMPARE(out.freeText, in.freeText);
    QCOMPARE(Mapping::duplicateKey(out), Mapping::duplicateKey(in));

    in.direction = HistoryEvent::Outbound;
    in.startTime = in.endTime;
    in.status = HistoryEvent::DeliveredStatus;
    sms = Mapping::eventToSms(in, QString());
    QCOMPARE(sms.type, int(Sbr::BoxSent));
    QCOMPARE(sms.status, int(Sbr::SmsStatusComplete));
    QCOMPARE(sms.contactName, QStringLiteral("(Unknown)"));
    QCOMPARE(Mapping::smsToEvent(sms, options, &out), Mapping::NotSkipped);
    QCOMPARE(out.status, int(HistoryEvent::DeliveredStatus));
    in.status = HistoryEvent::PermanentlyFailedStatus;
    QCOMPARE(Mapping::eventToSms(in, QString()).type, int(Sbr::BoxFailed));

    HistoryEvent call;
    call.type = HistoryEvent::CallEvent;
    call.direction = HistoryEvent::Inbound;
    call.startTime = 1706760000;
    call.endTime = 1706760062;
    call.remoteUid = QStringLiteral("+12025550101");
    Sbr::CallRecord record = Mapping::eventToCall(call, QString());
    QCOMPARE(record.type, int(Sbr::CallIncoming));
    QCOMPARE(record.duration, Q_INT64_C(62));
    QCOMPARE(record.presentation, int(Sbr::PresentationAllowed));
    QVERIFY(record.subscriptionId.isNull());
    QCOMPARE(Mapping::callToEvent(record, options, &out), Mapping::NotSkipped);
    QCOMPARE(out.endTime, call.endTime);
    QCOMPARE(Mapping::duplicateKey(out), Mapping::duplicateKey(call));

    call.isMissedCall = true;
    call.endTime = call.startTime + 20;     // ringing time, not in the backup
    call.incomingStatus = QStringLiteral("rejected");
    record = Mapping::eventToCall(call, QString());
    QCOMPARE(record.type, int(Sbr::CallRejected));
    QCOMPARE(record.duration, Q_INT64_C(0));
    QCOMPARE(Mapping::callToEvent(record, options, &out), Mapping::NotSkipped);
    QCOMPARE(out.incomingStatus, QStringLiteral("rejected"));
    QCOMPARE(Mapping::duplicateKey(out), Mapping::duplicateKey(call));

    call.remoteUid.clear();
    call.incomingStatus.clear();
    record = Mapping::eventToCall(call, QString());
    QCOMPARE(record.type, int(Sbr::CallMissed));
    QCOMPARE(record.presentation, int(Sbr::PresentationRestricted));
    QVERIFY(!record.number.isNull());       // written as number="", not "null"
    QVERIFY(record.number.isEmpty());
}

void TestCore::duplicateKeys()
{
    HistoryEvent a;
    a.type = HistoryEvent::SmsEvent;
    a.direction = HistoryEvent::Inbound;
    a.startTime = 100;
    a.endTime = 200;
    a.remoteUid = QStringLiteral("+49 176 12345678");
    a.freeText = QStringLiteral("hello");
    HistoryEvent b = a;
    b.remoteUid = QStringLiteral("017612345678");
    b.startTime = 200;      // the sent time does not matter
    QCOMPARE(Mapping::duplicateKey(a), Mapping::duplicateKey(b));
    b.freeText = QStringLiteral("hello!");
    QVERIFY(Mapping::duplicateKey(a) != Mapping::duplicateKey(b));
    b = a;
    b.endTime = 201;
    QVERIFY(Mapping::duplicateKey(a) != Mapping::duplicateKey(b));
    b = a;
    b.direction = HistoryEvent::Outbound;
    QVERIFY(Mapping::duplicateKey(a) != Mapping::duplicateKey(b));
    b = a;
    b.type = HistoryEvent::MmsEvent;
    QVERIFY(Mapping::duplicateKey(a) != Mapping::duplicateKey(b));

    HistoryEvent call;
    call.type = HistoryEvent::CallEvent;
    call.direction = HistoryEvent::Inbound;
    call.startTime = 100;
    call.endTime = 160;
    call.remoteUid = QStringLiteral("+4917612345678");
    HistoryEvent other = call;
    other.endTime = 161;
    QVERIFY(Mapping::duplicateKey(call) != Mapping::duplicateKey(other));
    call.isMissedCall = other.isMissedCall = true;
    QCOMPARE(Mapping::duplicateKey(call), Mapping::duplicateKey(other));
}

void TestCore::simSlots()
{
    QCOMPARE(Mapping::simSlot(QStringLiteral("/org/freedesktop/Telepathy/Account/ring/tel/ril_0")), 1);
    QCOMPARE(Mapping::simSlot(QStringLiteral("/org/freedesktop/Telepathy/Account/ring/tel/ril_1")), 2);
    QCOMPARE(Mapping::simSlot(QStringLiteral("/org/freedesktop/Telepathy/Account/ring/tel/account0")), 0);
    QCOMPARE(Mapping::simSlot(QStringLiteral("/org/freedesktop/Telepathy/Account/gabble/jabber/x")), 0);
    QCOMPARE(Mapping::ringAccount(1), QStringLiteral("/org/freedesktop/Telepathy/Account/ring/tel/ril_0"));
    QCOMPARE(Mapping::ringAccount(2), QStringLiteral("/org/freedesktop/Telepathy/Account/ring/tel/ril_1"));
}

namespace {

QDateTime at(int year, int month, int day, int hour, int minute)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute));
}

Schedule schedule(int frequency, int hour, int minute, int weekday = 1, int monthday = 1)
{
    Schedule s;
    s.frequency = frequency;
    s.minutes = hour * 60 + minute;
    s.weekday = weekday;
    s.monthday = monthday;
    return s;
}

} // namespace

void TestCore::scheduleDaily()
{
    const Schedule off;
    QVERIFY(!off.isEnabled());
    QVERIFY(!off.previous(at(2026, 11, 9, 12, 0)).isValid());
    QVERIFY(!off.next(at(2026, 11, 9, 12, 0)).isValid());

    const Schedule daily = schedule(Schedule::Daily, 3, 0);
    QCOMPARE(daily.previous(at(2026, 11, 9, 12, 0)), at(2026, 11, 9, 3, 0));
    QCOMPARE(daily.previous(at(2026, 11, 9, 2, 59)), at(2026, 11, 8, 3, 0));
    QCOMPARE(daily.previous(at(2026, 11, 9, 3, 0)), at(2026, 11, 9, 3, 0));    // at the time: due
    QCOMPARE(daily.next(at(2026, 11, 9, 3, 0)), at(2026, 11, 10, 3, 0));
    QCOMPARE(daily.next(at(2026, 11, 9, 2, 0)), at(2026, 11, 9, 3, 0));
    QCOMPARE(daily.next(at(2026, 12, 31, 23, 0)), at(2027, 1, 1, 3, 0));
}

void TestCore::scheduleWeeklyMonthly()
{
    // 2026-11-09 is a Monday
    const Schedule weekly = schedule(Schedule::Weekly, 22, 30, 3);      // Wednesdays
    QCOMPARE(weekly.next(at(2026, 11, 9, 12, 0)), at(2026, 11, 11, 22, 30));
    QCOMPARE(weekly.previous(at(2026, 11, 9, 12, 0)), at(2026, 11, 4, 22, 30));
    QCOMPARE(weekly.next(at(2026, 11, 11, 22, 30)), at(2026, 11, 18, 22, 30));
    const Schedule sunday = schedule(Schedule::Weekly, 8, 0, 7);
    QCOMPARE(sunday.next(at(2026, 11, 9, 12, 0)), at(2026, 11, 15, 8, 0));

    const Schedule monthly = schedule(Schedule::Monthly, 4, 15, 1, 15);
    QCOMPARE(monthly.next(at(2026, 11, 9, 12, 0)), at(2026, 11, 15, 4, 15));
    QCOMPARE(monthly.previous(at(2026, 11, 9, 12, 0)), at(2026, 10, 15, 4, 15));
    // Day 31: the last day of shorter months
    const Schedule last = schedule(Schedule::Monthly, 1, 0, 1, 31);
    QCOMPARE(last.next(at(2026, 11, 9, 12, 0)), at(2026, 11, 30, 1, 0));
    QCOMPARE(last.next(at(2027, 2, 1, 0, 0)), at(2027, 2, 28, 1, 0));
    QCOMPARE(last.next(at(2028, 2, 1, 0, 0)), at(2028, 2, 29, 1, 0));
    QCOMPARE(last.previous(at(2026, 12, 15, 0, 0)), at(2026, 11, 30, 1, 0));
    QVERIFY(!last.isBackupDay(QDate(2026, 11, 29)));
    QVERIFY(last.isBackupDay(QDate(2026, 12, 31)));
}

void TestCore::scheduleDue()
{
    const Schedule daily = schedule(Schedule::Daily, 3, 0);
    const QDateTime never;
    QVERIFY(daily.isDue(at(2026, 11, 9, 3, 10), never));
    // Ran at 03:10 (the first timer check): not again until the next night
    QVERIFY(!daily.isDue(at(2026, 11, 9, 3, 25), at(2026, 11, 9, 3, 10)));
    QVERIFY(!daily.isDue(at(2026, 11, 10, 2, 59), at(2026, 11, 9, 3, 10)));
    QVERIFY(daily.isDue(at(2026, 11, 10, 3, 0), at(2026, 11, 9, 3, 10)));
    // The phone was off at 03:00: the backup runs when it is on again
    QVERIFY(daily.isDue(at(2026, 11, 10, 9, 0), at(2026, 11, 9, 3, 10)));
    // Set up at noon: the first backup is at 03:00 the next night
    QVERIFY(!daily.isDue(at(2026, 11, 9, 12, 15), at(2026, 11, 9, 12, 0)));
    QVERIFY(daily.isDue(at(2026, 11, 10, 3, 15), at(2026, 11, 9, 12, 0)));
    QVERIFY(!Schedule().isDue(at(2026, 11, 9, 3, 10), never));
}

void TestCore::scheduleDaylightSaving()
{
    // Only checks something where the local time zone has a gap at that time (Central
    // Europe: 2027-03-28 02:00 -> 03:00).
    const QDate day(2027, 3, 28);
    const QDateTime gap(day, QTime(2, 30));
    if (gap.isValid() && gap.time() == QTime(2, 30))
        QSKIP("No daylight saving time gap at 02:30 on 2027-03-28 in this time zone");
    const Schedule s = schedule(Schedule::Daily, 2, 30);
    const QDateTime when = s.backupTime(day);
    QVERIFY(when.isValid());
    QCOMPARE(when.date(), day);
    QCOMPARE(when.time(), QTime(3, 30));
    QVERIFY(s.next(QDateTime(day, QTime(1, 0))) > QDateTime(day, QTime(1, 0)));
}

void TestCore::mmsAndRcsToEvents()
{
    Collected c;
    QString error;
    QVERIFY2(readAll(dataFile("mms-rcs-sample.xml"), &c, &error), qPrintable(error));
    QCOMPARE(c.mms.size(), 9);
    Mapping::ImportOptions options;
    options.markAsRead = false;
    HistoryEvent e;
    int missing = 0;

    // RCS chat message kept as MMS by Google Messages: text only, no files
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(0), options, &e, &missing), Mapping::NotSkipped);
    QCOMPARE(e.type, int(HistoryEvent::MmsEvent));
    QCOMPARE(e.direction, int(HistoryEvent::Inbound));
    QCOMPARE(e.remoteUid, QStringLiteral("+12025550101"));
    QVERIFY(e.participants.isEmpty());
    QCOMPARE(e.freeText, QStringLiteral("RCS hello ") + Emoji);
    QVERIFY(e.parts.isEmpty());
    QCOMPARE(e.endTime, qint64(1709280000));
    QCOMPARE(e.startTime, qint64(1709279999));      // date_sent in seconds
    QCOMPARE(e.status, int(HistoryEvent::ReceivedStatus));
    QVERIFY(e.subject.isEmpty());
    QCOMPARE(e.mmsTransactionId.left(6), QStringLiteral("proto:"));
    QCOMPARE(e.mmsMessageId, QStringLiteral("1709280000000"));
    QCOMPARE(Mapping::ownAddressOf(c.mms.at(0)), QStringLiteral("+12025550199"));

    // Sent RCS message
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(1), options, &e, &missing), Mapping::NotSkipped);
    QCOMPARE(e.direction, int(HistoryEvent::Outbound));
    QCOMPARE(e.remoteUid, QStringLiteral("+12025550101"));
    QCOMPARE(e.freeText, QStringLiteral("RCS reply"));
    QCOMPARE(e.status, int(HistoryEvent::SentStatus));
    QVERIFY(Mapping::ownAddressOf(c.mms.at(1)).isEmpty());

    // MMS with a picture: SMIL, picture and text are kept as parts
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(4), options, &e, &missing), Mapping::NotSkipped);
    QCOMPARE(e.direction, int(HistoryEvent::Outbound));
    QCOMPARE(e.remoteUid, QStringLiteral("+12025550105"));
    QCOMPARE(e.subject, QStringLiteral("Holiday"));
    QCOMPARE(e.freeText, QStringLiteral("Greetings from the beach"));
    QCOMPARE(e.parts.size(), 3);
    QVERIFY(e.parts.at(0).isSmil());
    QCOMPARE(e.parts.at(0).fileName, QStringLiteral("smil.xml"));
    QVERIFY(e.parts.at(0).data.startsWith("<smil>"));
    QCOMPARE(e.parts.at(1).contentType, QStringLiteral("image/png"));
    QCOMPARE(e.parts.at(1).fileName, QStringLiteral("beach_photo.png"));
    QCOMPARE(e.parts.at(1).contentId, QStringLiteral("<beach>"));
    QVERIFY(e.parts.at(1).data.startsWith("\x89PNG"));
    QVERIFY(e.parts.at(2).isText());
    QCOMPARE(e.parts.at(2).data, QByteArray("Greetings from the beach"));
    QCOMPARE(e.toList, QStringList() << QStringLiteral("+12025550105"));
    QCOMPARE(missing, 0);

    // Picture left out of the backup ("media" off): nothing left to restore
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(5), options, &e, &missing), Mapping::SkipNoContent);
    QCOMPARE(missing, 1);
    // ... but with text the message is restored without it
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(6), options, &e, &missing), Mapping::NotSkipped);
    QCOMPARE(missing, 2);
    QVERIFY(e.parts.isEmpty());
    QCOMPARE(e.freeText, QStringLiteral("The picture is not in this backup"));

    // Delivery report
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(7), options, &e, &missing), Mapping::SkipMmsReport);

    // RCS business chat: under the business name
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(8), options, &e, &missing), Mapping::NotSkipped);
    QCOMPARE(e.remoteUid, QStringLiteral("Example Bank"));
    QVERIFY(e.participants.isEmpty());
    QCOMPARE(e.freeText, QStringLiteral("Your code is 654321"));

    // Without part data (for comparing): same key
    Mapping::ImportOptions light = options;
    light.withPartData = false;
    HistoryEvent full;
    HistoryEvent bare;
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(4), options, &full), Mapping::NotSkipped);
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(4), light, &bare), Mapping::NotSkipped);
    QVERIFY(bare.parts.at(1).data.isEmpty());
    QCOMPARE(Mapping::duplicateKey(bare), Mapping::duplicateKey(full));

    // Drafts and unsent MMS
    Sbr::MmsRecord draft = c.mms.at(4);
    Sbr::setAttribute(&draft.attributes, QStringLiteral("msg_box"), QStringLiteral("3"));
    QCOMPARE(Mapping::mmsToEvent(draft, options, &e), Mapping::SkipDraft);
    Sbr::setAttribute(&draft.attributes, QStringLiteral("msg_box"), QStringLiteral("5"));
    QCOMPARE(Mapping::mmsToEvent(draft, options, &e), Mapping::SkipUnsent);
}

void TestCore::mmsGroups()
{
    Collected c;
    QVERIFY(readAll(dataFile("mms-rcs-sample.xml"), &c));
    Mapping::ImportOptions options;
    HistoryEvent e;

    // Group MMS: the address attribute lists everyone but the owner
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(2), options, &e), Mapping::NotSkipped);
    QCOMPARE(e.remoteUid, QStringLiteral("+12025550102"));     // the sender
    QCOMPARE(e.participants, QStringList() << QStringLiteral("+12025550101") << QStringLiteral("+12025550102")
                                          << QStringLiteral("+12025550103"));
    QCOMPARE(e.freeText, QStringLiteral("Group message from Bob"));
    QVERIFY(e.parts.isEmpty());     // text only: no files
    QVERIFY(e.isRead);              // marked read by default
    QCOMPARE(e.toList, QStringList() << QStringLiteral("+12025550199"));
    QCOMPARE(e.ccList, QStringList() << QStringLiteral("+12025550101") << QStringLiteral("+12025550103"));
    QVERIFY(Mapping::ownAddressOf(c.mms.at(2)).isEmpty());
    options.markAsRead = false;
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(2), options, &e), Mapping::NotSkipped);
    QVERIFY(!e.isRead);

    // RCS group chat: a chat id instead of numbers, the <addr> list names the members
    // (here including the owner, whom other messages of the backup reveal)
    QCOMPARE(Mapping::mmsToEvent(c.mms.at(3), options, &e), Mapping::NotSkipped);
    QCOMPARE(e.remoteUid, QStringLiteral("+12025550103"));
    QCOMPARE(e.participants, QStringList() << QStringLiteral("+12025550103") << QStringLiteral("+12025550199")
                                          << QStringLiteral("+12025550104"));
    QCOMPARE(e.freeText, QStringLiteral("RCS group chat message"));
}

void TestCore::eventToMmsRoundTrip()
{
    HistoryEvent in;
    in.type = HistoryEvent::MmsEvent;
    in.direction = HistoryEvent::Inbound;
    in.startTime = 1709280299;
    in.endTime = 1709280300;
    in.localUid = Mapping::ringAccount(1);
    in.remoteUid = QStringLiteral("+12025550106");
    in.participants = QStringList() << QStringLiteral("+12025550106") << QStringLiteral("+12025550107");
    in.toList = QStringList() << QStringLiteral("+12025550199") << QStringLiteral("+12025550107");
    in.subject = QStringLiteral("Photo");
    in.freeText = QStringLiteral("Look ") + Emoji;
    in.isRead = false;
    in.status = HistoryEvent::ReceivedStatus;
    in.mmsMessageId = QStringLiteral("m-123");
    HistoryPart smil;
    smil.contentType = QStringLiteral("application/smil");
    smil.contentId = QStringLiteral("<smil>");
    smil.fileName = QStringLiteral("smil.xml");
    smil.data = "<smil/>";
    HistoryPart picture;
    picture.contentType = QStringLiteral("image/jpeg");
    picture.contentId = QStringLiteral("<img>");
    picture.path = QStringLiteral("/home/user/.local/share/commhistory/data/42/IMG_1.jpg");
    picture.data = QByteArray("\xFF\xD8\xFF\xE0 not really a JPEG", 22);
    HistoryPart text;
    text.contentType = QStringLiteral("text/plain;charset=utf-8");
    text.contentId = QStringLiteral("<text_0>");
    text.fileName = QStringLiteral("text_0.txt");
    text.data = in.freeText.toUtf8();
    in.parts << smil << picture << text;

    const Sbr::MmsRecord record = Mapping::eventToMms(in, QString());
    QCOMPARE(record.value(QStringLiteral("msg_box")), QStringLiteral("1"));
    QCOMPARE(record.value(QStringLiteral("m_type")), QStringLiteral("132"));
    QCOMPARE(record.value(QStringLiteral("date")), QStringLiteral("1709280300000"));
    QCOMPARE(record.value(QStringLiteral("date_sent")), QStringLiteral("1709280299"));   // seconds, like Android
    QCOMPARE(record.value(QStringLiteral("address")), QStringLiteral("+12025550106~+12025550107"));
    QCOMPARE(record.value(QStringLiteral("read")), QStringLiteral("0"));
    QCOMPARE(record.value(QStringLiteral("text_only")), QStringLiteral("0"));
    QCOMPARE(record.value(QStringLiteral("sub")), QStringLiteral("Photo"));
    QCOMPARE(record.value(QStringLiteral("m_id")), QStringLiteral("m-123"));
    QVERIFY(record.value(QStringLiteral("tr_id")).isNull());
    QCOMPARE(record.value(QStringLiteral("sub_id")), QStringLiteral("1"));
    QCOMPARE(record.parts.size(), 3);
    QCOMPARE(record.parts.at(0).value(QStringLiteral("seq")), QStringLiteral("-1"));
    QCOMPARE(record.parts.at(0).value(QStringLiteral("text")), QStringLiteral("<smil/>"));
    QCOMPARE(record.parts.at(1).value(QStringLiteral("cl")), QStringLiteral("IMG_1.jpg"));
    QCOMPARE(QByteArray::fromBase64(record.parts.at(1).value(QStringLiteral("data")).toLatin1()), picture.data);
    QCOMPARE(record.parts.at(2).value(QStringLiteral("ct")), QStringLiteral("text/plain"));
    QCOMPARE(record.parts.at(2).value(QStringLiteral("chset")), QStringLiteral("106"));
    QCOMPARE(record.parts.at(2).value(QStringLiteral("text")), in.freeText);
    QCOMPARE(record.addresses.size(), 3);
    QCOMPARE(record.addresses.at(0).type, int(Sbr::AddrFrom));
    QCOMPARE(record.addresses.at(0).address, in.remoteUid);
    QCOMPARE(record.addresses.at(1).type, int(Sbr::AddrTo));

    Mapping::ImportOptions options;
    options.markAsRead = false;
    HistoryEvent out;
    QCOMPARE(Mapping::mmsToEvent(record, options, &out), Mapping::NotSkipped);
    QCOMPARE(out.type, in.type);
    QCOMPARE(out.direction, in.direction);
    QCOMPARE(out.startTime, in.startTime);
    QCOMPARE(out.endTime, in.endTime);
    QCOMPARE(out.remoteUid, in.remoteUid);
    QCOMPARE(out.participants, in.participants);
    QCOMPARE(out.subject, in.subject);
    QCOMPARE(out.freeText, in.freeText);
    QCOMPARE(out.isRead, false);
    QCOMPARE(out.parts.size(), 3);
    QCOMPARE(out.parts.at(1).data, picture.data);
    QCOMPARE(out.parts.at(1).fileName, QStringLiteral("IMG_1.jpg"));
    QCOMPARE(out.mmsMessageId, in.mmsMessageId);
    QCOMPARE(Mapping::duplicateKey(out), Mapping::duplicateKey(in));

    // Sent, text only (restored RCS messages have no parts): a text part is made
    HistoryEvent sent;
    sent.type = HistoryEvent::MmsEvent;
    sent.direction = HistoryEvent::Outbound;
    sent.startTime = sent.endTime = 1709280060;
    sent.remoteUid = QStringLiteral("+12025550101");
    sent.freeText = QStringLiteral("RCS reply");
    sent.status = HistoryEvent::SentStatus;
    const Sbr::MmsRecord sentRecord = Mapping::eventToMms(sent, QString());
    QCOMPARE(sentRecord.value(QStringLiteral("msg_box")), QStringLiteral("2"));
    QCOMPARE(sentRecord.value(QStringLiteral("m_type")), QStringLiteral("128"));
    QCOMPARE(sentRecord.value(QStringLiteral("text_only")), QStringLiteral("1"));
    QCOMPARE(sentRecord.value(QStringLiteral("date_sent")), QStringLiteral("0"));
    QCOMPARE(sentRecord.parts.size(), 1);
    QCOMPARE(sentRecord.addresses.at(0).address, QLatin1String(Sbr::OwnAddressToken));
    QCOMPARE(sentRecord.addresses.at(1).address, sent.remoteUid);
    QCOMPARE(Mapping::mmsToEvent(sentRecord, options, &out), Mapping::NotSkipped);
    QCOMPARE(Mapping::duplicateKey(out), Mapping::duplicateKey(sent));
}

void TestCore::archiveKeys()
{
    // An entry of an Android backup and the entry this app writes for the same message
    // after restoring it (milliseconds lost, other attributes) have the same key.
    Collected sms;
    QVERIFY(readAll(dataFile("sms-sample.xml"), &sms));
    const Mapping::ImportOptions options;
    for (const Sbr::SmsRecord &r : sms.sms) {
        HistoryEvent e;
        if (Mapping::smsToEvent(r, options, &e) != Mapping::NotSkipped)
            continue;
        QCOMPARE(Mapping::archiveKey(Mapping::eventToSms(e, QString())), Mapping::archiveKey(r));
    }
    Collected calls;
    QVERIFY(readAll(dataFile("calls-sample.xml"), &calls));
    for (const Sbr::CallRecord &r : calls.calls) {
        HistoryEvent e;
        if (Mapping::callToEvent(r, options, &e) != Mapping::NotSkipped)
            continue;
        QCOMPARE(Mapping::archiveKey(Mapping::eventToCall(e, QString())), Mapping::archiveKey(r));
    }
    Collected mms;
    QVERIFY(readAll(dataFile("mms-rcs-sample.xml"), &mms));
    for (const Sbr::MmsRecord &r : mms.mms) {
        HistoryEvent e;
        if (Mapping::mmsToEvent(r, options, &e) != Mapping::NotSkipped)
            continue;
        QCOMPARE(Mapping::archiveKey(Mapping::eventToMms(e, QString())), Mapping::archiveKey(r));
    }

    // Entries that cannot be restored still have their own keys
    const Sbr::SmsRecord draft = sms.sms.at(4);
    QCOMPARE(draft.type, int(Sbr::BoxDraft));
    Sbr::SmsRecord otherDraft = draft;
    QCOMPARE(Mapping::archiveKey(otherDraft), Mapping::archiveKey(draft));
    otherDraft.body = QStringLiteral("changed draft");
    QVERIFY(Mapping::archiveKey(otherDraft) != Mapping::archiveKey(draft));
    QVERIFY(Mapping::archiveKey(mms.mms.at(7)) != Mapping::archiveKey(mms.mms.at(4)));     // report vs. message
    QVERIFY(Mapping::archiveKey(sms.sms.at(0)) != Mapping::archiveKey(sms.sms.at(1)));
}

void TestCore::archiveCopy()
{
    // Entries copied into an archive file keep all their attributes.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("sms.xml"));
    Collected original;
    QVERIFY(readAll(dataFile("sms-sample.xml"), &original));
    Collected mmsOriginal;
    QVERIFY(readAll(dataFile("mms-rcs-sample.xml"), &mmsOriginal));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        SbrWriter writer(&file, SbrWriter::MessagesKind);
        QVERIFY(writer.begin(original.sms.size() + mmsOriginal.mms.size(), QStringLiteral("set"),
                             Q_INT64_C(1709287200000), QStringLiteral("Test"), QStringLiteral("archive")));
        for (const Sbr::SmsRecord &r : original.sms)
            QVERIFY(writer.writeElement("sms", r.attributes));
        for (const Sbr::MmsRecord &r : mmsOriginal.mms)
            QVERIFY(writer.writeMms(r));
        QVERIFY(writer.end());
    }
    Collected copy;
    SbrReader::Info info;
    QString error;
    QVERIFY2(readAll(path, &copy, &error, &info), qPrintable(error));
    QVERIFY(info.isArchive());
    QCOMPARE(info.count, original.sms.size() + mmsOriginal.mms.size());
    QCOMPARE(copy.sms.size(), original.sms.size());
    for (int i = 0; i < copy.sms.size(); ++i) {
        QCOMPARE(copy.sms.at(i).attributes, original.sms.at(i).attributes);
        QCOMPARE(Mapping::archiveKey(copy.sms.at(i)), Mapping::archiveKey(original.sms.at(i)));
    }
    QCOMPARE(copy.mms.size(), mmsOriginal.mms.size());
    for (int i = 0; i < copy.mms.size(); ++i) {
        QCOMPARE(copy.mms.at(i).attributes, mmsOriginal.mms.at(i).attributes);
        QCOMPARE(copy.mms.at(i).parts.size(), mmsOriginal.mms.at(i).parts.size());
        QCOMPARE(copy.mms.at(i).addresses.size(), mmsOriginal.mms.at(i).addresses.size());
        QCOMPARE(Mapping::archiveKey(copy.mms.at(i)), Mapping::archiveKey(mmsOriginal.mms.at(i)));
    }
    // The emoji written as two surrogate references by the Android app is UTF-8 now.
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    QVERIFY(!content.contains("&#55357;"));
    QVERIFY(content.contains(Emoji.toUtf8()));
    QVERIFY(content.contains(QStringLiteral("<smses count=\"%1\" backup_set=\"set\" backup_date=\"1709287200000\" type=\"archive\">")
                             .arg(original.sms.size() + mmsOriginal.mms.size()).toUtf8()));
}

void TestCore::partFileNames()
{
    QCOMPARE(Mapping::partFileName(QStringLiteral("beach photo.png"), QStringLiteral("image/png"), 1),
             QStringLiteral("beach_photo.png"));
    QCOMPARE(Mapping::partFileName(QStringLiteral("<image001>"), QStringLiteral("image/jpeg"), 1),
             QStringLiteral("image001.jpg"));
    QCOMPARE(Mapping::partFileName(QString(), QStringLiteral("video/3gpp"), 3), QStringLiteral("part3.3gp"));
    QCOMPARE(Mapping::partFileName(QStringLiteral("../../etc/passwd"), QStringLiteral("text/plain"), 1),
             QStringLiteral("passwd.txt"));
    QCOMPARE(Mapping::partFileName(QStringLiteral(".hidden"), QStringLiteral("image/gif"), 2),
             QStringLiteral("hidden.gif"));
    QCOMPARE(Mapping::partFileName(QString::fromUtf8("Ärger.jpg"), QStringLiteral("image/jpeg"), 1),
             QStringLiteral("_rger.jpg"));
    QCOMPARE(Mapping::partFileName(QString(200, QLatin1Char('a')) + QStringLiteral(".jpeg"),
                                   QStringLiteral("image/jpeg"), 1).size(), 80);
    QCOMPARE(Mapping::partFileName(QStringLiteral("x"), QStringLiteral("application/octet-stream"), 1),
             QStringLiteral("x"));
    QCOMPARE(Mapping::decodeText(QByteArray("caf\xE9"), QStringLiteral("text/plain;charset=iso-8859-1")),
             QString::fromUtf8("café"));
    QCOMPARE(Mapping::decodeText(QByteArray("caf\xC3\xA9"), QStringLiteral("text/plain")), QString::fromUtf8("café"));
}

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
