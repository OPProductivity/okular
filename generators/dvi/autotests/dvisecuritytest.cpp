/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "../bigEndianByteReader.h"
#include "../debug_dvi.h"
#include "../dviFile.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
Q_LOGGING_CATEGORY(OkularDviDebug, "org.kde.okular.generators.dvi.core", QtWarningMsg)
Q_LOGGING_CATEGORY(OkularDviShellDebug, "org.kde.okular.generators.dvi.shell", QtWarningMsg)

static void append32(QByteArray &data, quint32 value)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        data.append(char(value >> shift));
    }
}
static QByteArray minimalDvi(quint32 offset)
{
    QByteArray data;
    data.append(char(247));
    data.append(char(2));
    append32(data, 25400000);
    append32(data, 473628672);
    append32(data, 1000);
    data.append(char(0));
    data.append(char(139)); // BOP at offset 15
    for (int i = 0; i < 10; ++i) {
        append32(data, 0x11223344);
    }
    append32(data, 0xffffffffU);
    data.append(char(140));
    const quint32 post = data.size();
    data.append(char(248));
    append32(data, offset);
    append32(data, 25400000);
    append32(data, 473628672);
    append32(data, 1000);
    append32(data, 0);
    append32(data, 0);
    data.append(char(0));
    data.append(char(0)); // stack depth
    data.append(char(0));
    data.append(char(1)); // page count
    data.append(char(249));
    append32(data, post);
    data.append(char(2));
    data.append(QByteArray(4, char(223)));
    return data;
}
class DviSecurityTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void truncatedReaders()
    {
        quint8 bytes[] = {0x12, 0x34, 0x56, 0x78};
        for (int size = 0; size < 4; ++size) {
            bigEndianByteReader reader;
            reader.command_pointer = bytes;
            reader.end_pointer = bytes + size;
            reader.readUINT32();
            QVERIFY(reader.readFailed);
            QVERIFY(reader.command_pointer <= reader.end_pointer);
            reader.command_pointer = bytes;
            reader.writeUINT32(0xffffffffU);
            QCOMPARE(bytes[0], quint8(0x12));
        }
        bigEndianByteReader reader;
        reader.command_pointer = bytes;
        reader.end_pointer = bytes + 4;
        QCOMPARE(reader.readUINT32(), quint32(0x12345678));
        reader.command_pointer = bytes;
        QCOMPARE(reader.readINT(1), qint32(0x12));
        bytes[0] = 0xff;
        reader.command_pointer = bytes;
        QCOMPARE(reader.readINT(1), qint32(-1));
        reader.command_pointer = bytes;
        reader.readINT(0);
        QVERIFY(reader.command_pointer <= reader.end_pointer);
    }
    void pageOffsets()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (quint32 offset : {0U, 14U, 16U, 60U, 99U, 0xffffffffU}) {
            QFile file(dir.filePath(QStringLiteral("bad.dvi")));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(minimalDvi(offset));
            file.close();
            dvifile document(file.fileName(), nullptr);
            QVERIFY2(!document.errorMsg.isEmpty(), qPrintable(QString::number(offset)));
            document.renumber(); // Must not attempt a write after a rejected parse.
        }
    }
    void renumberExactlyFourBytes()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("valid.dvi")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original = minimalDvi(15);
        file.write(original);
        file.close();
        dvifile document(file.fileName(), nullptr);
        QVERIFY2(document.errorMsg.isEmpty(), qPrintable(document.errorMsg));
        document.renumber();
        QVERIFY(document.errorMsg.isEmpty());
        const quint8 *data = document.dvi_Data();
        for (int i = 0; i < original.size(); ++i) {
            const quint8 expected = (i >= 16 && i <= 19) ? (i == 19 ? 1 : 0) : quint8(original[i]);
            QCOMPARE(data[i], expected);
        }
    }
    void truncatedPreamble()
    {
        QTemporaryDir dir;
        for (int size = 0; size < 15; ++size) {
            QFile file(dir.filePath(QStringLiteral("truncated.dvi")));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(minimalDvi(15).left(size));
            file.close();
            dvifile document(file.fileName(), nullptr);
            QVERIFY(!document.errorMsg.isEmpty());
        }
    }
};
QTEST_GUILESS_MAIN(DviSecurityTest)
#include "dvisecuritytest.moc"
