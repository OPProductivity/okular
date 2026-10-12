/*
    SPDX-FileCopyrightText: 2020 Simone Gaiarin <simgunz@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// clazy:excludeall=qstring-allocations

#include <QTest>

#include <QMenu>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QWidget>

#include <KActionCollection>
#include <KConfigGroup>
#include <KSelectAction>
#include <KSharedConfig>

#include "../core/page.h"
#include "../part/pageview.h"
#include "../part/part.h"
#include "../part/toggleactionmenu.h"
#include "../settings.h"
#include "../shell/okular_main.h"
#include "../shell/shell.h"
#include "../shell/shellutils.h"
#include "closedialoghelper.h"

namespace Okular
{
class PartTest
{
public:
    Okular::Document *partDocument(Okular::Part *part) const
    {
        return part->m_document;
    }
    PageView *pageView(Okular::Part *part) const
    {
        return part->m_pageView;
    }
};
}

class AnnotationToolBarTest : public QObject, public Okular::PartTest
{
    Q_OBJECT

public:
    static QTabWidget *tabWidget(Shell *s)
    {
        return s->m_tabWidget;
    }

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testAnnotationToolBar();
    void testAnnotationToolBar_data();
    void testAnnotationToolBarActionsEnabledState();
    void testAnnotationToolBarActionsEnabledState_data();
    void testAnnotationToolBarConfigActionsEnabledState();
    void testAnnotationToolBarConfigActionsEnabledState_data();

private:
    bool simulateAddPopupAnnotation(Okular::Part *part, int mouseX, int mouseY);
};

Shell *findShell(Shell *ignore = nullptr)
{
    const QWidgetList wList = QApplication::topLevelWidgets();
    for (QWidget *widget : wList) {
        Shell *s = qobject_cast<Shell *>(widget);
        if (s && s != ignore) {
            return s;
        }
    }
    return nullptr;
}

void AnnotationToolBarTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    // Settings reads these XML defaults on first use. Stage the fixtures in
    // Qt's test data location so the suite does not depend on installation.
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/okular");
    QVERIFY(QDir().mkpath(dataDir));
    for (const QString &name : {QStringLiteral("drawingtools.xml"), QStringLiteral("tools.xml"), QStringLiteral("toolsQuick.xml")}) {
        const QString target = dataDir + QLatin1Char('/') + name;
        QFile::remove(target);
        QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "../part/data/") + name, target));
    }
    // Don't pollute people's okular settings
    Okular::Settings::instance(QStringLiteral("annotationtoolbartest"));
}

void AnnotationToolBarTest::init()
{
    // Default settings for every test
    Okular::Settings::self()->setDefaults();
    // This suite tests annotation controls, not saved windows. In the shared
    // Qt test profile another suite may have left a restorable PDF session.
    const QString partConfigPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/okularpartrc");
    KSharedConfigPtr partConfig = KSharedConfig::openConfig(partConfigPath);
    partConfig->group(QStringLiteral("General")).writeEntry("ShellRestoreOpenDocuments", false);
    partConfig->sync();
}

void AnnotationToolBarTest::cleanup()
{
    Shell *s;
    while ((s = findShell())) {
        delete s;
    }
}

bool AnnotationToolBarTest::simulateAddPopupAnnotation(Okular::Part *part, int mouseX, int mouseY)
{
    Q_UNUSED(mouseX);
    const int pageNumber = partDocument(part)->currentPage();
    const int annotationCount = partDocument(part)->page(pageNumber)->annotations().size();
    // Fit-page mode centers the small fixture page in the viewport. Fixed
    // coordinates can hit the gray margin, especially after the toolbar opens.
    const QPoint pageCenter = pageView(part)->viewport()->rect().center();
    const QPoint clickPos(pageCenter.x(), pageCenter.y() + (mouseY - 175) / 5);
    QTest::mouseMove(pageView(part)->viewport(), clickPos);
    QTest::mouseClick(pageView(part)->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    const bool annotationAdded = partDocument(part)->page(pageNumber)->annotations().size() == annotationCount + 1;
    return annotationAdded;
}

void AnnotationToolBarTest::testAnnotationToolBar()
{
    // Using tabs we test that the annotation toolbar works on each Okular::Part
    Okular::Settings::self()->setShellOpenFileInTabs(true);

    const QStringList paths = {QStringLiteral(KDESRCDIR "data/file1.pdf"), QStringLiteral(KDESRCDIR "data/file2.pdf")};
    QString serializedOptions = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());

    Okular::Status status = Okular::main(paths, serializedOptions);
    QCOMPARE(status, Okular::Success);
    Shell *s = findShell();
    QVERIFY(s);
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(s));
    s->resize(1200, 800);
    QFETCH(int, tabIndex);
    s->m_tabWidget->tabBar()->setCurrentIndex(tabIndex);
    QCOMPARE(s->m_tabWidget->currentIndex(), tabIndex);
    Okular::Part *part = dynamic_cast<Okular::Part *>(s->m_tabs[tabIndex].part);
    QVERIFY(part);
    QVERIFY(partDocument(part)->isOpened());
    QTRY_VERIFY(part->widget()->isVisible());
    QTRY_VERIFY(pageView(part)->viewport()->width() > 500);
    QTRY_VERIFY(partDocument(part)->page(partDocument(part)->currentPage())->hasPixmap(pageView(part)));

    QToolBar *annToolBar = s->findChild<QToolBar *>(QStringLiteral("annotationToolBar"));
    QVERIFY(annToolBar);

    // Check config action default enabled states
    QAction *aQuickTools = part->actionCollection()->action(QStringLiteral("annotation_favorites"));
    QAction *aAddToQuickTools = part->actionCollection()->action(QStringLiteral("annotation_bookmark"));
    QAction *aAdvancedSettings = part->actionCollection()->action(QStringLiteral("annotation_settings_advanced"));
    QAction *aContinuousMode = part->actionCollection()->action(QStringLiteral("annotation_settings_pin"));
    QVERIFY(aQuickTools->isEnabled());
    QVERIFY(!aAddToQuickTools->isEnabled());
    QVERIFY(!aAdvancedSettings->isEnabled());
    QVERIFY(aContinuousMode->isEnabled());

    // Ensure that the 'Quick Annotations' action is correctly populated
    // (at least the 'Configure Annotations...' action must be present)
    QVERIFY(!aQuickTools->menu()->actions().isEmpty());

    // Test annotation toolbar visibility triggers
    QAction *toggleAnnotationToolBar = part->actionCollection()->action(QStringLiteral("mouse_toggle_annotate"));
    QAction *aHideToolBar = part->actionCollection()->action(QStringLiteral("hide_annotation_toolbar"));
    QVERIFY(toggleAnnotationToolBar);
    QVERIFY(aHideToolBar);
    toggleAnnotationToolBar->setChecked(false);
    QTRY_VERIFY(!annToolBar->isVisible());
    toggleAnnotationToolBar->trigger();
    QTRY_VERIFY2(annToolBar->isVisible(), "Annotation action failed to show.");
    toggleAnnotationToolBar->trigger();
    QTRY_VERIFY2(!annToolBar->isVisible(), "Annotation action failed to hide.");

    toggleAnnotationToolBar->setChecked(true);
    QTRY_VERIFY(annToolBar->isVisible());
    aHideToolBar->trigger();
    QTRY_VERIFY2(!annToolBar->isVisible(), "Hide toolbar action failed to hide.");

    toggleAnnotationToolBar->setChecked(false);
    QTRY_VERIFY(!annToolBar->isVisible());
    QTest::keyClick(part->widget(), Qt::Key_1, Qt::AltModifier);
    QTRY_VERIFY2(annToolBar->isVisible(), "ToolBar not shown when triggering annotation using shortcut.");
    toggleAnnotationToolBar->setChecked(false);
    QTRY_VERIFY(!annToolBar->isVisible());
    QTest::keyClick(part->widget(), Qt::Key_3);
    QTRY_VERIFY2(!annToolBar->isVisible(), "ToolBar shown when triggering quick annotation using shortcut.");

    // set mouse mode to browse before starting the tests on the annotation actions
    QAction *aMouseNormal = part->actionCollection()->action(QStringLiteral("mouse_drag"));
    QVERIFY(aMouseNormal);
    aMouseNormal->trigger();
    QTRY_COMPARE(Okular::Settings::mouseMode(), static_cast<int>(Okular::Settings::EnumMouseMode::Browse));

    // Click an annotation action to enable it
    QAction *aPopupNote = part->actionCollection()->action(QStringLiteral("annotation_popup_note"));
    QVERIFY(aPopupNote);
    aPopupNote->trigger();
    int mouseX = 350;
    int mouseY = 100;
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    QTRY_COMPARE(aMouseNormal->isChecked(), false);

    // Click again the same annotation action to disable it
    aPopupNote->trigger();
    mouseY = 150;
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), false);
    QTRY_COMPARE(aMouseNormal->isChecked(), true);

    // Trigger the action using a shortcut
    QTest::keyClick(part->widget(), Qt::Key_7, Qt::AltModifier);
    mouseY = 200;
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    QTRY_COMPARE(aMouseNormal->isChecked(), false);

    // Click Esc to disable all annotations
    QTest::keyClick(pageView(part), Qt::Key_Escape);
    mouseY = 250;
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), false);
    QTRY_COMPARE(aMouseNormal->isChecked(), true);

    // Trigger the action using a quick annotation shortcut
    QTest::keyClick(part->widget(), Qt::Key_6);
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    QTRY_COMPARE(aMouseNormal->isChecked(), false);

    // Test pin/continuous mode action
    QVERIFY(aContinuousMode->isEnabled());
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    aContinuousMode->trigger();
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), false);

    // Test adding a tool to the quick tool list using the bookmark action
    QScopedPointer<TestingUtils::CloseDialogHelper> closeDialogHelper;
    closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(QDialogButtonBox::Ok));
    aPopupNote->trigger();
    QVERIFY(aPopupNote->isChecked());
    int quickActionCount = aQuickTools->menu()->actions().size();
    aAddToQuickTools->trigger();
    QTRY_COMPARE(aQuickTools->menu()->actions().size(), quickActionCount + 1);
    // Trigger the quick tool that was just added
    aQuickTools->menu()->actions().at(6)->trigger();
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
}

void AnnotationToolBarTest::testAnnotationToolBar_data()
{
    QTest::addColumn<int>("tabIndex");
    QTest::addRow("first tab") << 0;
    QTest::addRow("second tab") << 1;
}

void AnnotationToolBarTest::testAnnotationToolBarActionsEnabledState()
{
    QFETCH(QString, document);

    const QStringList paths = {document};
    QString serializedOptions = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());

    Okular::Status status = Okular::main(paths, serializedOptions);
    QCOMPARE(status, Okular::Success);
    Shell *s = findShell();
    QVERIFY(s);
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(s));

    Okular::Part *part = nullptr;
    for (const auto &tab : std::as_const(s->m_tabs)) {
        auto *candidate = qobject_cast<Okular::Part *>(tab.part);
        if (candidate && QDir::cleanPath(candidate->url().toLocalFile()) == QDir::cleanPath(document)) {
            part = candidate;
            break;
        }
    }
    QVERIFY2(part, "The requested document was not opened in any tab");

    KActionCollection *ac = part->actionCollection();
    QAction *aQuickTools = ac->action(QStringLiteral("annotation_favorites"));
    QAction *aHighlighter = ac->action(QStringLiteral("annotation_highlighter"));
    QAction *aUnderline = ac->action(QStringLiteral("annotation_underline"));
    QAction *aSquiggle = ac->action(QStringLiteral("annotation_squiggle"));
    QAction *aStrikeout = ac->action(QStringLiteral("annotation_strike_out"));
    QAction *aTypewriter = ac->action(QStringLiteral("annotation_typewriter"));
    QAction *aInlineNote = ac->action(QStringLiteral("annotation_inline_note"));
    QAction *aPopupNote = ac->action(QStringLiteral("annotation_popup_note"));
    QAction *aFreehandLine = ac->action(QStringLiteral("annotation_freehand_line"));
    QAction *aGeomShapes = ac->action(QStringLiteral("annotation_geometrical_shape"));
    QAction *aStamp = ac->action(QStringLiteral("annotation_stamp"));

    QFETCH(bool, aQuickToolsEnabled);
    QFETCH(bool, aHighlighterEnabled);
    QFETCH(bool, aUnderlineEnabled);
    QFETCH(bool, aSquiggleEnabled);
    QFETCH(bool, aStrikeoutEnabled);
    QFETCH(bool, aTypewriterEnabled);
    QFETCH(bool, aInlineNoteEnabled);
    QFETCH(bool, aPopupNoteEnabled);
    QFETCH(bool, aFreehandLineEnabled);
    QFETCH(bool, aGeomShapesEnabled);
    QFETCH(bool, aStampEnabled);

    QCOMPARE(aQuickTools->isEnabled(), aQuickToolsEnabled);
    QCOMPARE(aHighlighter->isEnabled(), aHighlighterEnabled);
    QCOMPARE(aUnderline->isEnabled(), aUnderlineEnabled);
    QCOMPARE(aSquiggle->isEnabled(), aSquiggleEnabled);
    QCOMPARE(aStrikeout->isEnabled(), aStrikeoutEnabled);
    QCOMPARE(aTypewriter->isEnabled(), aTypewriterEnabled);
    QCOMPARE(aInlineNote->isEnabled(), aInlineNoteEnabled);
    QCOMPARE(aPopupNote->isEnabled(), aPopupNoteEnabled);
    QCOMPARE(aFreehandLine->isEnabled(), aFreehandLineEnabled);
    QCOMPARE(aGeomShapes->isEnabled(), aGeomShapesEnabled);
    QCOMPARE(aStamp->isEnabled(), aStampEnabled);

    // trigger a reparsing of the tools to ensure that the enabled/disabled state is not changed (bug: 424296)
    QAction *aMouseSelect = ac->action(QStringLiteral("mouse_select"));
    QAction *aMouseNormal = ac->action(QStringLiteral("mouse_drag"));
    aMouseSelect->trigger();
    aMouseNormal->trigger();

    QCOMPARE(aQuickTools->isEnabled(), aQuickToolsEnabled);
    QCOMPARE(aHighlighter->isEnabled(), aHighlighterEnabled);
    QCOMPARE(aUnderline->isEnabled(), aUnderlineEnabled);
    QCOMPARE(aSquiggle->isEnabled(), aSquiggleEnabled);
    QCOMPARE(aStrikeout->isEnabled(), aStrikeoutEnabled);
    QCOMPARE(aTypewriter->isEnabled(), aTypewriterEnabled);
    QCOMPARE(aInlineNote->isEnabled(), aInlineNoteEnabled);
    QCOMPARE(aPopupNote->isEnabled(), aPopupNoteEnabled);
    QCOMPARE(aFreehandLine->isEnabled(), aFreehandLineEnabled);
    QCOMPARE(aGeomShapes->isEnabled(), aGeomShapesEnabled);
    QCOMPARE(aStamp->isEnabled(), aStampEnabled);
}

void AnnotationToolBarTest::testAnnotationToolBarActionsEnabledState_data()
{
    QTest::addColumn<QString>("document");
    QTest::addColumn<bool>("aQuickToolsEnabled");
    QTest::addColumn<bool>("aHighlighterEnabled");
    QTest::addColumn<bool>("aUnderlineEnabled");
    QTest::addColumn<bool>("aSquiggleEnabled");
    QTest::addColumn<bool>("aStrikeoutEnabled");
    QTest::addColumn<bool>("aTypewriterEnabled");
    QTest::addColumn<bool>("aInlineNoteEnabled");
    QTest::addColumn<bool>("aPopupNoteEnabled");
    QTest::addColumn<bool>("aFreehandLineEnabled");
    QTest::addColumn<bool>("aGeomShapesEnabled");
    QTest::addColumn<bool>("aStampEnabled");

    QTest::addRow("pdf") << QStringLiteral(KDESRCDIR "data/file1.pdf") << true << true << true << true << true << true << true << true << true << true << true;
    QTest::addRow("protected-pdf") << QStringLiteral(KDESRCDIR "data/protected.pdf") << false << false << false << false << false << false << false << false << false << false << false;
    QTest::addRow("image") << QStringLiteral(KDESRCDIR "data/potato.jpg") << true << false << false << false << false << true << true << true << true << true << true;
}

void AnnotationToolBarTest::testAnnotationToolBarConfigActionsEnabledState()
{
    const QStringList paths = {QStringLiteral(KDESRCDIR "data/file1.pdf")};
    QString serializedOptions = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());

    Okular::Status status = Okular::main(paths, serializedOptions);
    QCOMPARE(status, Okular::Success);
    Shell *s = findShell();
    QVERIFY(s);
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(s));

    Okular::Part *part = qobject_cast<Okular::Part *>(s->m_tabs.at(s->m_tabWidget->currentIndex()).part);
    QVERIFY(part);
    QCOMPARE(QDir::cleanPath(part->url().toLocalFile()), QDir::cleanPath(QStringLiteral(KDESRCDIR "data/file1.pdf")));

    KActionCollection *ac = part->actionCollection();
    QAction *aWidth = ac->action(QStringLiteral("annotation_settings_width"));
    QAction *aColor = ac->action(QStringLiteral("annotation_settings_color"));
    QAction *aInnerColor = ac->action(QStringLiteral("annotation_settings_inner_color"));
    QAction *aOpacity = ac->action(QStringLiteral("annotation_settings_opacity"));
    QAction *aFont = ac->action(QStringLiteral("annotation_settings_font"));

    QFETCH(QString, annotationActionName);
    QFETCH(bool, widthEnabled);
    QFETCH(bool, colorEnabled);
    QFETCH(bool, innerColorEnabled);
    QFETCH(bool, opacityEnabled);
    QFETCH(bool, fontEnabled);

    QAction *annotationAction = ac->action(annotationActionName);
    annotationAction->trigger();

    QCOMPARE(aWidth->isEnabled(), widthEnabled);
    QCOMPARE(aColor->isEnabled(), colorEnabled);
    QCOMPARE(aInnerColor->isEnabled(), innerColorEnabled);
    QCOMPARE(aOpacity->isEnabled(), opacityEnabled);
    QCOMPARE(aFont->isEnabled(), fontEnabled);
}

void AnnotationToolBarTest::testAnnotationToolBarConfigActionsEnabledState_data()
{
    QTest::addColumn<QString>("annotationActionName");
    QTest::addColumn<bool>("widthEnabled");
    QTest::addColumn<bool>("colorEnabled");
    QTest::addColumn<bool>("innerColorEnabled");
    QTest::addColumn<bool>("opacityEnabled");
    QTest::addColumn<bool>("fontEnabled");

    QTest::addRow("annotation_highlighter") << QStringLiteral("annotation_highlighter") << false << true << false << true << false;
    QTest::addRow("annotation_underline") << QStringLiteral("annotation_underline") << false << true << false << true << false;
    QTest::addRow("annotation_squiggle") << QStringLiteral("annotation_squiggle") << false << true << false << true << false;
    QTest::addRow("annotation_strike_out") << QStringLiteral("annotation_strike_out") << false << true << false << true << false;
    QTest::addRow("annotation_typewriter") << QStringLiteral("annotation_typewriter") << false << true << false << true << true;
    QTest::addRow("annotation_inline_note") << QStringLiteral("annotation_inline_note") << false << true << false << true << true;
    QTest::addRow("annotation_popup_note") << QStringLiteral("annotation_popup_note") << false << true << false << true << false;
    QTest::addRow("annotation_freehand_line") << QStringLiteral("annotation_freehand_line") << true << true << false << true << false;
    QTest::addRow("annotation_line") << QStringLiteral("annotation_straight_line") << true << true << false << true << false;
    QTest::addRow("annotation_rectangle") << QStringLiteral("annotation_rectangle") << true << true << true << true << false;
}

QTEST_MAIN(AnnotationToolBarTest)
#include "annotationtoolbartest.moc"
