/*
    SPDX-FileCopyrightText: 2020 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../unrarflavours.h"
#include "../extractedfile_p.h"
#include <QFile>
#include <KZip>
#include <QTemporaryDir>
#include <QTest>

#include "core/document.h"
#include "core/generator.h"
#include "core/observer.h"
#include "core/page.h"

#include "../document.h"

#include "settings_core.h"

class ComicBookGeneratorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testRotatedImage();
    void testEmptyRarListing();
    void testDeepArchive();
    void testExtractedFileContainment();
    void cleanupTestCase();
};

void ComicBookGeneratorTest::initTestCase()
{
    Okular::SettingsCore::instance(QStringLiteral("ComicBookGeneratorTest"));
}

void ComicBookGeneratorTest::cleanupTestCase()
{
}

void ComicBookGeneratorTest::testRotatedImage()
{
    ComicBook::Document document;
    const QString testFile = QStringLiteral(KDESRCDIR "autotests/data/rotated_cb.cbz");
    QVERIFY(document.open(testFile));

    QList<Okular::Page *> pagesVector;
    document.pages(&pagesVector);

    const Okular::Page *p = pagesVector[0];
    QVERIFY(p->height() > p->width());

    const QImage image = document.pageImage(0);
    QVERIFY(image.height() > image.width());
}

void ComicBookGeneratorTest::testEmptyRarListing()
{
    UnarFlavour flavour;
    QVERIFY(flavour.processListing({}).isEmpty());
    QVERIFY(flavour.processListing({QStringLiteral("header")}).isEmpty());
}

void ComicBookGeneratorTest::testDeepArchive()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("deep.cbz"));
    KZip zip(file);
    QVERIFY(zip.open(QIODevice::WriteOnly));
    QString path;
    for (int i = 0; i < 140; ++i) {
        path += QStringLiteral("d/");
    }
    QVERIFY(zip.writeFile(path + QStringLiteral("image.png"), QByteArray("test")));
    QVERIFY(zip.close());
    ComicBook::Document document;
    QVERIFY(!document.open(file));
}

void ComicBookGeneratorTest::testExtractedFileContainment()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir root(dir.path());
    QVERIFY(root.mkdir(QStringLiteral("extract")));
    QVERIFY(root.mkdir(QStringLiteral("elsewhere")));
    QFile inside(dir.filePath(QStringLiteral("extract/page.png")));
    QVERIFY(inside.open(QIODevice::WriteOnly));
    inside.write("page");
    inside.close();
    QFile outside(dir.filePath(QStringLiteral("elsewhere/private.png")));
    QVERIFY(outside.open(QIODevice::WriteOnly));
    outside.write("private");
    outside.close();
    const QString extract = dir.filePath(QStringLiteral("extract"));
    QCOMPARE(Okular::safeExtractedFilePath(extract, QStringLiteral("page.png")), QFileInfo(inside).canonicalFilePath());
    for (const QString &name : {QStringLiteral("../elsewhere/private.png"), QStringLiteral("/page.png"), QStringLiteral("sub/../../elsewhere/private.png"),
                                QStringLiteral("C:private.png"), QStringLiteral("missing.png")}) {
        QVERIFY2(Okular::safeExtractedFilePath(extract, name).isEmpty(), qPrintable(name));
    }
    const QString link = dir.filePath(QStringLiteral("extract/link.png"));
    if (QFile::link(outside.fileName(), link) && QFileInfo(link).isSymLink()) {
        QVERIFY(Okular::safeExtractedFilePath(extract, QStringLiteral("link.png")).isEmpty());
    }
}

QTEST_MAIN(ComicBookGeneratorTest)
#include "comicbooktest.moc"

/* kate: replace-tabs on; tab-width 4; */
