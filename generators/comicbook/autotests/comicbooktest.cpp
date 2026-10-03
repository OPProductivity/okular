/*
    SPDX-FileCopyrightText: 2020 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../unrarflavours.h"
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

QTEST_MAIN(ComicBookGeneratorTest)
#include "comicbooktest.moc"

/* kate: replace-tabs on; tab-width 4; */
