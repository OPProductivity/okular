/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "core/action.h"
#include "core/annotations.h"
#include "core/document.h"
#include "core/form.h"
#include "core/generator.h"
#include "core/observer.h"
#include "core/page.h"
#include "core/sound.h"
#include "generators/fictionbook/converter.h"
#include "generators/fictionbook/document.h"
#include "settings_core.h"
#include <KConfig>
#include <KConfigGroup>
#include <KZip>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QMimeDatabase>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <memory>
#include <poppler-link.h>

static bool writePdf(const QString &path, const QList<QByteArray> &objects)
{
    QByteArray pdf("%PDF-1.4\n");
    QList<int> offsets;
    for (int i = 0; i < objects.size(); ++i) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const int xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (int offset : offsets) {
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(pdf) == pdf.size();
}
static Okular::Document::OpenResult open(Okular::Document &doc, const QString &file, const QString &mime)
{
    return doc.openDocument(file, QUrl::fromLocalFile(file), QMimeDatabase().mimeTypeForName(mime));
}
class SecurityDocumentTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        Okular::SettingsCore::instance(QStringLiteral("securitydocumenttest"));
        // Ensure these tests exercise rebuilt generators exclusively.
        QCoreApplication::setLibraryPaths({QCoreApplication::applicationDirPath()});
    }
    void closingRenditionOwnership()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("rendition.pdf"));
        QVERIFY(writePdf(file,
                         {"<< /Type /Catalog /Pages 2 0 R >>",
                          "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                          "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Annots [4 0 R] /AA << /C 5 0 R >> >>",
                          "<< /Type /Annot /Subtype /Screen /Rect [0 0 100 100] /P 3 0 R /A 5 0 R >>",
                          "<< /S /Rendition /OP 0 /AN 4 0 R /R 6 0 R >>",
                          "<< /S /MR /C 7 0 R >>",
                          "<< /S /MCD /D 8 0 R /CT (video/mp4) >>",
                          "<< /Type /Filespec /F (test.mp4) >>"}));
        Okular::Document doc(nullptr);
        QCOMPARE(open(doc, file, QStringLiteral("application/pdf")), Okular::Document::OpenSuccess);
        const auto *action = doc.page(0)->pageAction(Okular::Page::Closing);
        QVERIFY(action);
        QCOMPARE(action->actionType(), Okular::Action::Rendition);
        // Annotations are loaded lazily. The retained native closing action must
        // remain alive until that later resolution (the former pointer dangled).
        QVERIFY(action->nativeHandle());
        const auto *native = static_cast<const Poppler::LinkRendition *>(action->nativeHandle());
        QCOMPARE(native->action(), Poppler::LinkRendition::PlayRendition);
        QVERIFY(native->rendition());
        Okular::DocumentObserver observer;
        doc.addObserver(&observer);
        doc.requestPixmaps({new Okular::PixmapRequest(&observer, 0, 200, 200, 1.0, 1, Okular::PixmapRequest::NoFeature)});
        QTRY_VERIFY(doc.page(0)->hasPixmap(&observer));
        QVERIFY(static_cast<const Okular::RenditionAction *>(action)->annotation());
        doc.removeObserver(&observer);
        doc.closeDocument();
    }
    void signatureNullEvent()
    {
        Okular::Document doc(nullptr);
        const QString file = QStringLiteral(KDESRCDIR "data/hello_with_dummy_signature.pdf");
        QCOMPARE(open(doc, file, QStringLiteral("application/pdf")), Okular::Document::OpenSuccess);
        QVERIFY(!doc.page(0)->formFields().isEmpty());
        QCOMPARE(doc.page(0)->formFields().first()->type(), Okular::FormField::FormSignature);
        Okular::ScriptAction script(Okular::JavaScript, QStringLiteral("var securityNullEvent = 1;"));
        doc.processAction(&script);
        doc.closeDocument();
    }
    void emptyScriptModelsAndChoices()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("choice.pdf"));
        QVERIFY(writePdf(file,
                         {"<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R] >> >>",
                          "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                          "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Annots [4 0 R] >>",
                          "<< /Type /Annot /Subtype /Widget /FT /Ch /T (empty) /Opt [] /Rect [0 0 100 20] /P 3 0 R >>"}));
        Okular::Document doc(nullptr);
        QCOMPARE(open(doc, file, QStringLiteral("application/pdf")), Okular::Document::OpenSuccess);
        QVERIFY(!doc.layersModel());
        QCOMPARE(doc.page(0)->formFields().first()->type(), Okular::FormField::FormChoice);
        Okular::ScriptAction script(Okular::JavaScript, QStringLiteral("Doc.getOCGs(); Doc.getNthFieldName(-1); Doc.getField('empty').getItemAt(-1); Doc.getField('empty').getItemAt(0, false);"));
        doc.processAction(&script);
        const auto *field = static_cast<const Okular::FormFieldChoice *>(doc.page(0)->formFields().first());
        const QList<int> original = field->currentChoices();
        Okular::ScriptAction sparse(Okular::JavaScript, QStringLiteral("var sparse = []; sparse.length = 4294967295; Doc.getField('empty').currentValueIndices = sparse; app.okular_popUpMenuEx(sparse);"));
        QElapsedTimer elapsed;
        elapsed.start();
        doc.processAction(&sparse);
        QVERIFY(elapsed.elapsed() < 1000);
        QCOMPARE(field->currentChoices(), original);
        doc.closeDocument();
    }
    void automaticExternalActions()
    {
        Okular::Document doc(nullptr);
        QSignalSpy blocked(&doc, &Okular::Document::error);
        Okular::BrowseAction browse(QUrl(QStringLiteral("unsafe-handler:payload")));
        doc.processAction(&browse, false);
        doc.processAction(&browse); // Deliberate custom-scheme link is rejected, too.
        QCOMPARE(blocked.size(), 1);
        for (const QString &destination : {QStringLiteral("https://example.invalid/sound"), QStringLiteral("http://127.0.0.1/sound"), QStringLiteral("file:///C:/sound"), QStringLiteral("file://server/share/sound")}) {
            Okular::SoundAction action(1.0, false, false, false, new Okular::Sound(destination));
            doc.processAction(&action, false); // Must not show consent or contact a backend.
        }
    }
    void archiveRoundTrip()
    {
        if (!QMimeDatabase().mimeTypeForName(QStringLiteral("application/vnd.kde.okular-archive")).isValid()) {
            QSKIP("The runtime MIME database has no Okular archive registration");
        }
        QTemporaryDir dir;
        const QString source = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
        const QString archive = dir.filePath(QStringLiteral("roundtrip.okular"));
        Okular::Document doc(nullptr);
        QCOMPARE(open(doc, source, QStringLiteral("application/pdf")), Okular::Document::OpenSuccess);
        const unsigned int pages = doc.pages();
        QVERIFY(doc.saveDocumentArchive(archive));
        doc.closeDocument();
        QCOMPARE(doc.openDocumentArchive(archive, QUrl::fromLocalFile(archive)), Okular::Document::OpenSuccess);
        QCOMPARE(doc.pages(), pages);
        doc.closeDocument();
    }
    void archiveMetadataBudget()
    {
        if (!QMimeDatabase().mimeTypeForName(QStringLiteral("application/vnd.kde.okular-archive")).isValid()) {
            QSKIP("The runtime MIME database has no Okular archive registration");
        }
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("metadata.okular"));
        KZip zip(file);
        QVERIFY(zip.open(QIODevice::WriteOnly));
        QVERIFY(zip.writeFile(QStringLiteral("content.xml"), QByteArray(17 * 1024 * 1024, 'x')));
        QVERIFY(zip.close());
        Okular::Document doc(nullptr);
        QVERIFY(doc.openDocumentArchive(file, QUrl::fromLocalFile(file)) != Okular::Document::OpenSuccess);
    }
    void normalFictionBook()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("normal.fb2")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("<FictionBook><body><section><p>Normal content</p><table><tr><td colspan=\"2\">Cell</td></tr></table></section></body></FictionBook>");
        file.close();
        FictionBook::Converter converter;
        std::unique_ptr<QTextDocument> document(converter.convert(file.fileName()));
        QVERIFY(document);
        QVERIFY(document->toPlainText().contains(QLatin1String("Normal content")));
    }
    void fictionBookZipDirectory()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("directory.fb2.zip"));
        KZip zip(file);
        QVERIFY(zip.open(QIODevice::WriteOnly));
        QVERIFY(zip.writeDir(QStringLiteral("payload.fb2")));
        QVERIFY(zip.close());
        FictionBook::Document document(file);
        QVERIFY(!document.open());
    }
    void fictionBookLimits_data()
    {
        QTest::addColumn<QByteArray>("xml");
        QByteArray nested("<FictionBook><body>");
        for (int i = 0; i < 150; ++i) {
            nested += "<section>";
        }
        nested += "<p>Text</p>";
        for (int i = 0; i < 150; ++i) {
            nested += "</section>";
        }
        nested += "</body></FictionBook>";
        QTest::newRow("deep") << nested;
        QTest::newRow("huge-colspan") << QByteArray("<FictionBook><body><section><table><tr><td colspan=\"2147483647\">Text</td></tr></table></section></body></FictionBook>");
    }
    void fictionBookLimits()
    {
        QFETCH(QByteArray, xml);
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("budget.fb2")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(xml);
        file.close();
        FictionBook::Converter converter;
        std::unique_ptr<QTextDocument> document(converter.convert(file.fileName()));
        QVERIFY(!document);
    }
    void epubMalformedMedia()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("media.epub"));
        KZip zip(file);
        QVERIFY(zip.open(QIODevice::WriteOnly));
        QVERIFY(zip.writeFile(QStringLiteral("mimetype"), QByteArray("application/epub+zip")));
        QVERIFY(zip.writeFile(
            QStringLiteral("META-INF/container.xml"),
            QByteArray("<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile full-path=\"book.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>")));
        QVERIFY(zip.writeFile(QStringLiteral("book.opf"),
                              QByteArray("<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"id\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>Test</dc:title><dc:identifier "
                                         "id=\"id\">test</dc:identifier><dc:language>en</dc:language></metadata><manifest><item id=\"chapter\" href=\"chapter.xhtml\" media-type=\"application/xhtml+xml\"/></manifest><spine><itemref "
                                         "idref=\"chapter\"/></spine></package>")));
        QVERIFY(zip.writeFile(QStringLiteral("chapter.xhtml"),
                              QByteArray("<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p><a href=\".../\">anchor</a></p><svg/><svg><image xlink:href=\"absent.png\"/></svg><video/><video><source "
                                         "src=\"absent.mp4\"/></video><pre>&lt;video&gt;&lt;/video&gt;</pre><audio/><pre>&lt;audio&gt;&lt;/audio&gt;</pre></body></html>")));
        QVERIFY(zip.close());
        Okular::Document doc(nullptr);
        QElapsedTimer elapsed;
        elapsed.start();
        QCOMPARE(open(doc, file, QStringLiteral("application/epub+zip")), Okular::Document::OpenSuccess);
        QVERIFY(elapsed.elapsed() < 5000);
        QVERIFY(doc.pages() > 0);
        doc.closeDocument();
    }
    void malformedXpsImageBrushes()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("brushes.xps"));
        KZip zip(file);
        QVERIFY(zip.open(QIODevice::WriteOnly));
        QVERIFY(zip.writeFile(QStringLiteral("_rels/.rels"), QByteArray("<Relationships><Relationship Type=\"http://schemas.microsoft.com/xps/2005/06/fixedrepresentation\" Target=\"sequence.fdseq\"/></Relationships>")));
        QVERIFY(zip.writeFile(QStringLiteral("sequence.fdseq"), QByteArray("<FixedDocumentSequence><DocumentReference Source=\"doc.fdoc\"/></FixedDocumentSequence>")));
        QVERIFY(zip.writeFile(QStringLiteral("doc.fdoc"), QByteArray("<FixedDocument><PageContent Source=\"page.fpage\"/></FixedDocument>")));
        QVERIFY(zip.writeFile(QStringLiteral("page.fpage"),
                              QByteArray("<FixedPage Width=\"200\" Height=\"200\"><Path Data=\"M 0,0 L 100,0 100,100 Z\"><Path.Fill><ImageBrush ImageSource=\"\" Viewbox=\"1,2\" Viewport=\"1,2\"/></Path.Fill></Path><Path Data=\"M 0,0 L "
                                         "100,0 100,100 Z\"><Path.Fill><ImageBrush ImageSource=\"absent/image.png\" Viewbox=\"0,0,1,1\" Viewport=\"0,0,100,100\"/></Path.Fill></Path></FixedPage>")));
        QVERIFY(zip.close());
        Okular::Document doc(nullptr);
        QCOMPARE(open(doc, file, QStringLiteral("application/vnd.ms-xpsdocument")), Okular::Document::OpenSuccess);
        Okular::DocumentObserver observer;
        doc.addObserver(&observer);
        doc.requestPixmaps({new Okular::PixmapRequest(&observer, 0, 200, 200, 1.0, 1, Okular::PixmapRequest::NoFeature)});
        QTRY_VERIFY(doc.page(0)->hasPixmap(&observer));
        doc.removeObserver(&observer);
        doc.closeDocument();
    }
    void missingXpsParts()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("missing.xps"));
        KZip zip(file);
        QVERIFY(zip.open(QIODevice::WriteOnly));
        QVERIFY(zip.writeFile(QStringLiteral("_rels/.rels"), QByteArray("<Relationships><Relationship Type=\"http://schemas.microsoft.com/xps/2005/06/fixedrepresentation\" Target=\"missing.fdseq\"/></Relationships>")));
        QVERIFY(zip.close());
        Okular::Document doc(nullptr);
        QVERIFY(open(doc, file, QStringLiteral("application/vnd.ms-xpsdocument")) != Okular::Document::OpenSuccess);
    }
};
QTEST_MAIN(SecurityDocumentTest)
#include "securitydocumenttest.moc"
