/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "generators/txt/converter.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <memory>
class TextSecurityTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void plainTextBudget()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("text.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Normal document text");
        file.close();
        Txt::Converter converter;
        std::unique_ptr<QTextDocument> normal(converter.convert(file.fileName()));
        QVERIFY(normal);
        QCOMPARE(normal->toPlainText(), QStringLiteral("Normal document text"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.resize(17 * 1024 * 1024));
        file.close();
        QVERIFY(!converter.convert(file.fileName()));
    }
};
QTEST_MAIN(TextSecurityTest)
#include "textsecuritytest.moc"
