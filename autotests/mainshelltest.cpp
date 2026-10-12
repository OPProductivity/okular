/*
    SPDX-FileCopyrightText: 2014 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// clazy:excludeall=qstring-allocations

#include <QTest>
#include <config-okular.h>

#include <KConfigGroup>
#include <KActionCollection>
#include <KLineEdit>
#include <KLocalizedString>
#include <KMessageBox>
#include <KRecentFilesAction>

#if HAVE_DBUS
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#endif // HAVE_DBUS
#if defined(Q_OS_WIN)
#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>
#include <dwmapi.h>
#include <windows.h>
#endif
#include <QDir>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QPrintDialog>
#include <QProcessEnvironment>
#include <QScreen>
#include <QScopeGuard>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QStyleOption>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <qwidget.h>

#include <algorithm>

#include "../core/document_p.h"
#include "../part/findbar.h"
#include "../part/layers.h"
#include "../part/pageview.h"
#include "../part/part.h"
#include "../part/presentationwidget.h"
#include "../part/side_reviews.h"
#include "../part/sidebar.h"
#include "../settings.h"
#include "../shell/okular_main.h"
#include "../shell/shell.h"
#include "../shell/shellutils.h"
#include "closedialoghelper.h"

#include <algorithm>
#include <cmath>
#include <sys/types.h>
#ifndef Q_OS_WIN
#include <unistd.h>
#else
#include <process.h>
#endif

namespace Okular
{
class PartTest
{
public:
    Okular::Document *partDocument(Okular::Part *part) const
    {
        return part->m_document;
    }
    QWidget *presentationWidget(Okular::Part *part) const
    {
        return part->m_presentationWidget;
    }
    FindBar *findWidget(Okular::Part *part) const
    {
        return part->m_findBar;
    }
    Sidebar *sidebar(Okular::Part *part) const { return part->m_sidebar; }
    QWidget *bottomBar(Okular::Part *part) const { return part->m_bottomBar; }
    Reviews *reviews(Okular::Part *part) const { return part->m_reviewsWidget; }
    Layers *layers(Okular::Part *part) const { return part->m_layers; }
    PageView *pageView(Okular::Part *part) const { return part->m_pageView; }
};
}

class ClosePrintDialogHelper : public QObject
{
    Q_OBJECT

public:
    explicit ClosePrintDialogHelper(int expectedTab)
        : foundDialog(false)
        , m_expectedTab(expectedTab)
    {
    }
    bool foundDialog;

public Q_SLOTS:
    void closePrintDialog();

private:
    int m_expectedTab;
};

class MainShellTest : public QObject, public Okular::PartTest
{
    Q_OBJECT

public:
    static QTabWidget *tabWidget(Shell *s)
    {
        return s->m_tabWidget;
    }

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testShell_data();
    void testShell();
    void testFileRemembersPagePosition_data();
    void testFileRemembersPagePosition();
    void test2FilesError_data();
    void test2FilesError();
    void testMiddleButtonCloseUndo();
    void testSessionRestore_data();
    void testSessionRestore();
    void testOpenInvalidFiles_data();
    void testOpenInvalidFiles();
    void testCorruptPdfDoesNotDiscardValidTab();
#if defined(Q_OS_WIN)
    void testCorruptPdfWithSeparateWindows();
    void testForwardedCorruptPdfAfterClientDisconnect();
#endif
    void testOpenTheSameFileSeveralTimes();
#if defined(Q_OS_WIN)
    void testEpubAtLongWindowsPath();
    void testForwardedWindowReturnsFromOffscreen();
    void testForwardedFileOpensInExistingTabs();
    void testForwardedBatchReportsAlreadyOpenFiles();
    void testForwardingRejectsOversizeRequest();
    void testShortcutLaunchReusesExistingWindow();
    void testAdditionalShellDoesNotRegisterWindowsTabOpenServer();
    void testSeparateWindowSessionsRestore();
    void testColorSchemeMenuTracksOtherWindow();
    void testTabPaletteTracksColorScheme();
    void testTabWidthsStayStableAcrossSelection();
    void testPageNumberControlWidthStaysStable();
    void testDuplicateOpenShowsPageNotice();
    void testSharedViewerSettingsAcrossWindows();
    void testSavedViewModeDoesNotChangeOtherWindow();
    void testClosingTabsKeepsSinglePageCentered();
    void testSharedReviewAndLayerSettingsAcrossWindows();
    void testRecentlyClosedWindowRestoresRemainingTabs();
    void testCloseAllHiddenWindowsRestoresThreeSessions();
    void testCloseAllAfterClosingSomeTabsRestoresRemainingTabs();
    void testFiveWindowShutdownSurvivesPartialRestoreSave();
    void testSequentialFiveWindowCloseRestoresAll();
    void testPrimaryTakeoverKeepsPreviousOwnersWindows();
    void testStartupReadsPreviousOwnersFinalSession();
    void testClosedWindowsCannotOverwriteCompletedShutdown();
    void testEmptyFirstWindowDoesNotRestore();
    void testTemporarilyMissingTabSurvivesRestore();
    void testReopeningMissingTabClearsPendingEntry();
    void testClosedWindowExpiresAfterFiveMinutes();
    void testClockCorrectionKeepsRecentlyClosedWindow();
    void testClosingPrimaryTransfersForwarding();
    void testElevatedRequestDeclinesUnreadableFile();
    void testNewWindowActionAppearsInFileMenu();
    void testMoveTabToNewWindowPreservesPart();
    void testSelectAndCloseSeveralTabs();
    void testCloseSelectedTabsWithCloseButton();
    void testControlClickSelectsSeparateTabs();
    void testMoveSelectedTabsBetweenWindows();
    void testMoveSelectedTabsViaMenu();
    void testMoveToExistingWindowMenu();
    void testOrdinaryOpenFocusesTabInOtherWindow();
    void testRecentFilesStayConsistentAcrossWindows();
    void testOpenRecentFocusesDocumentInOtherWindow();
    void testDragTabOutsideWindowDetaches();
    void testDroppedFileOpensNewTab();
#endif
    void testOpenDocumentSessionRestoresTabs();
    void testFileLaunchRestoresTabsBeforeOpeningFile();
    void testDisabledOpenDocumentSessionRestore();
    void testOpenDocumentSessionIgnoresNewFileTabPreference();
    void testOpenDocumentSessionSkipsMissingFile();
#if defined(Q_OS_WIN)
    void testOpenDocumentSessionDeduplicatesSavedUrls();
#endif
    void testOpenAllRecentDocumentsAsTabs();
    void testOpenAllIgnoresRecentSelection();
    void testOpenAllRecentDocumentsFromScrolledList();
    void testRecentFilesExpireAfterThreeDays();
    void testRecentFilesLimitedToFifty();
    void testSelectRecentDocumentsAcrossScrollRange();
    void testSelectRecentDocumentsWithCtrlClick();
    void testSelectRecentDocumentsWithShiftClick();
    void testRecentContextMenuSelectedActions();
    void testRecentContextMenuOpensSelected();
    void testRecentContextMenuCopiesSelectedPaths();
    void testTabControlsRemainAvailable();
    void testOverflowArrowsSelectEveryTab();
    void testRtlOverflowControlsStayInsideTabBar();
    void testCenterButtonFramesActiveTab();
    void testFileLaunchShowsDocumentViewFirst();
    void testWelcomeHiddenBeforeDocumentLoad();

private:
};

QList<Shell *> getShells()
{
    QList<Shell *> shells;
    const QList<KMainWindow *> mainWindows = KMainWindow::memberList();
    for (KMainWindow *kmw : mainWindows) {
        Shell *shell = qobject_cast<Shell *>(kmw);
        if (shell) {
            shells.append(shell);
        }
    }
    return shells;
}

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

#if defined(Q_OS_WIN)
static QByteArray tabOpenRequest(quint32 magic, const QString &options, const QStringList &paths)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << magic << options << paths;
    QByteArray request;
    QDataStream frame(&request, QIODevice::WriteOnly);
    frame.setVersion(QDataStream::Qt_6_0);
    frame << quint32(payload.size());
    return request + payload;
}
#endif

static KSharedConfigPtr testShellSessionConfig()
{
#if defined(Q_OS_WIN)
    const QString testInstance = qEnvironmentVariable("OKULAR_TEST_INSTANCE");
    if (!testInstance.isEmpty()) {
        const QString testDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/test-sessions");
        const QString instanceHash = QString::fromLatin1(QCryptographicHash::hash(testInstance.toUtf8(), QCryptographicHash::Sha256).toHex());
        const QString role = ShellUtils::windowsProcessIsElevated() ? QStringLiteral("elevated") : QStringLiteral("standard");
        return KSharedConfig::openConfig(testDirectory + QStringLiteral("/okular-%1-%2.ini").arg(role, instanceHash));
    }
    if (ShellUtils::windowsProcessIsElevated()) {
        return KSharedConfig::openConfig(QStringLiteral("okular-elevated-sessionrc"));
    }
#endif
    return KSharedConfig::openConfig();
}

void MainShellTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)));
    // Don't pollute people's okular settings
    Okular::Settings::instance(QStringLiteral("mainshelltest-settingsrc"));

    // Register in bus as okular
#if HAVE_DBUS
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    QVERIFY(bus->registerService(ShellUtils::currentProcessDbusName()) == QDBusConnectionInterface::ServiceRegistered);
#endif
    // Tell the presentationWidget and queryClose to not be annoying
    KSharedConfigPtr c = KSharedConfig::openConfig(QStringLiteral("mainshelltest.kmessagebox"));
    KConfigGroup cg = c->group(QStringLiteral("General"));
    cg.writeEntry("presentationInfo", 4);
    cg.writeEntry("ShowTabWarning", 4);
}

void MainShellTest::cleanupTestCase()
{
}

void MainShellTest::init()
{
    // Default settings for every test
    Okular::Settings::self()->setDefaults();
    const QString partConfigPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QLatin1String("/okularpartrc");
    KSharedConfigPtr partConfig = KSharedConfig::openConfig(partConfigPath);
    partConfig->group(QStringLiteral("General")).writeEntry("ShellRestoreOpenDocuments", true);
    partConfig->sync();
    KSharedConfigPtr config = testShellSessionConfig();
    config->group(QStringLiteral("Shell Open Documents Session")).deleteGroup();
#if defined(Q_OS_WIN)
    const KConfigGroup windows = config->group(QStringLiteral("Shell Open Documents Session Windows"));
    for (const QString &id : windows.readEntry("WindowIds", QStringList())) {
        config->group(QStringLiteral("Shell Open Documents Session Window ") + id).deleteGroup();
    }
    config->group(QStringLiteral("Shell Open Documents Session Windows")).deleteGroup();
#endif
    config->sync();

    // Clean docdatas
    const QList<QUrl> urls = {QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/file1.pdf")),
                              QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/tocreload.pdf")),
                              QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/contents.epub"))};

    for (const QUrl &url : urls) {
        QFileInfo fileReadTest(url.toLocalFile());
        const QString docDataPath = Okular::DocumentPrivate::docDataFileName(url, fileReadTest.size());
        QFile::remove(docDataPath);
    }
}

void MainShellTest::cleanup()
{
    Shell *s;
    while ((s = findShell())) {
        delete s;
    }
}

#if defined(Q_OS_WIN)
static QRect visibleWindowsFrame(QWidget *widget)
{
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    RECT rect = {};
    const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    using DwmGetWindowAttributeFunction = HRESULT(WINAPI *)(HWND, DWORD, PVOID, DWORD);
    const auto getWindowAttribute = dwm ? reinterpret_cast<DwmGetWindowAttributeFunction>(GetProcAddress(dwm, "DwmGetWindowAttribute")) : nullptr;
    if (!getWindowAttribute || FAILED(getWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        GetWindowRect(hwnd, &rect);
    }
    if (dwm) {
        FreeLibrary(dwm);
    }
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

void MainShellTest::testEpubAtLongWindowsPath()
{
#if !defined(OKULAR_TEST_EPUB_LONG_PATH)
    QSKIP("EPUB generator is not built");
#else
    DWORD longPaths = 0;
    DWORD size = sizeof(longPaths);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"LongPathsEnabled", RRF_RT_REG_DWORD, nullptr, &longPaths, &size) != ERROR_SUCCESS || !longPaths) {
        QSKIP("Windows long-path policy is disabled");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString nested = directory.path();
    while (nested.size() < 300) {
        nested += QLatin1Char('/') + QString(40, QLatin1Char('x'));
    }
    QVERIFY(QDir().mkpath(nested));
    const QString path = nested + QStringLiteral("/book-\u00e9.epub");
    QVERIFY(path.size() > MAX_PATH);
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "../generators/epub/autotests/data/test.epub"), path));
    Shell shell;
    shell.openUrl(QUrl::fromLocalFile(path));
    QCOMPARE(shell.m_tabs.size(), 1);
    const auto *document = partDocument(qobject_cast<Okular::Part *>(shell.m_tabs.at(0).part));
    QVERIFY(document);
    QCOMPARE(document->pages(), 3U);
    QCOMPARE(shell.m_tabs.at(0).part->url().toLocalFile(), path);
#endif
}

void MainShellTest::testForwardedWindowReturnsFromOffscreen()
{
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        QSKIP("Native window frame placement requires a desktop window manager");
    }
    const QString uniqueOptions = ShellUtils::serializeOptions(false, false, false, true, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), uniqueOptions), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    // Let both startup placement passes finish before moving this test window.
    QTest::qWait(200);
    QScreen *screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const QRect bounds = screen->geometry();
    const QRect startupGeometry = shell->geometry();
    QCOMPARE(visibleWindowsFrame(shell), bounds);
    QVERIFY(!shell->isMaximized());

    for (const int offset : {bounds.width() / 6, bounds.width() / 3, bounds.width() / 2, -bounds.width() / 6}) {
        shell->setGeometry(bounds.left() + offset, startupGeometry.top(), startupGeometry.width(), startupGeometry.height());
        QCoreApplication::processEvents();
        QVERIFY(!bounds.contains(visibleWindowsFrame(shell)));

        shell->raisePrivateWindowsShell();
        QCoreApplication::processEvents();
        QCOMPARE(visibleWindowsFrame(shell), bounds);
        QVERIFY(!shell->isMaximized());
    }

    const QRect deliberateGeometry(bounds.left() + bounds.width() / 8, bounds.top() + bounds.height() / 8, bounds.width() / 2, bounds.height() / 2);
    shell->setGeometry(deliberateGeometry);
    QCoreApplication::processEvents();
    shell->raisePrivateWindowsShell();
    QCoreApplication::processEvents();
    QCOMPARE(visibleWindowsFrame(shell), bounds);

    shell->setGeometry(bounds.left() + bounds.width() / 2, startupGeometry.top(), startupGeometry.width(), startupGeometry.height());
    shell->showMinimized();
    QCoreApplication::processEvents();
    shell->raisePrivateWindowsShell();
    QCoreApplication::processEvents();
    QVERIFY(!shell->isMinimized());
    QCOMPARE(visibleWindowsFrame(shell), bounds);

    shell->showMaximized();
    QCoreApplication::processEvents();
    shell->raisePrivateWindowsShell();
    QCoreApplication::processEvents();
    QVERIFY(!shell->isMaximized());
    QCOMPARE(visibleWindowsFrame(shell), bounds);

    shell->showFullScreen();
    QCoreApplication::processEvents();
    shell->raisePrivateWindowsShell();
    QCoreApplication::processEvents();
    QVERIFY(shell->isFullScreen());

    delete shell;
    Shell *restored = new Shell(uniqueOptions);
    QVERIFY(restored->isValid());
    restored->setGeometry(bounds.left() + bounds.width() / 2, bounds.top(), bounds.width(), bounds.height());
    restored->show();
    QTest::qWait(200);
    QVERIFY(!restored->isMaximized());
    QVERIFY(!restored->isFullScreen());
    QCOMPARE(visibleWindowsFrame(restored), bounds);
}
#endif

#if defined(Q_OS_WIN)
void MainShellTest::testForwardedFileOpensInExistingTabs()
{
    // Windows file launches use the single-window tab path even when the
    // optional tab preference has not been enabled in a fresh profile.
    Okular::Settings::self()->setShellOpenFileInTabs(false);
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QVERIFY(shell->m_windowsTabOpenServer);
    QVERIFY(shell->m_windowsTabOpenServer->isListening());

    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    shell->openUrl(firstUrl);
    QCOMPARE(shell->m_tabs.size(), 1);

    QLocalSocket socket;
    socket.connectToServer(ShellUtils::windowsTabOpenServerName(), QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(1000));
    const QByteArray payload = tabOpenRequest(0x4f4b5450, options, {secondUrl.toLocalFile()});
    QCOMPARE(socket.write(payload), payload.size());
    socket.flush();

    QTRY_COMPARE(shell->m_tabs.size(), 2);
    QVERIFY(socket.bytesAvailable() > 0 || socket.waitForReadyRead(1000));
    QCOMPARE(socket.read(1), QByteArrayLiteral("A"));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(1).part->url().toLocalFile()), QDir::cleanPath(secondUrl.toLocalFile()));
    QVERIFY(findShell(shell) == nullptr);
    socket.disconnectFromServer();
}

void MainShellTest::testForwardedBatchReportsAlreadyOpenFiles()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    const QUrl first = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl second = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    shell->openUrl(first);

    for (const QUrl &url : {second, first, second}) {
        QLocalSocket socket;
        socket.connectToServer(ShellUtils::windowsTabOpenServerName(), QIODevice::ReadWrite);
        QVERIFY(socket.waitForConnected(1000));
        const QByteArray payload = tabOpenRequest(0x4f4b5450, options, {url.toLocalFile()});
        QCOMPARE(socket.write(payload), payload.size());
        socket.flush();
        QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0, 1000);
        QCOMPARE(socket.read(1), QByteArrayLiteral("A"));
        socket.disconnectFromServer();
    }

    QCOMPARE(shell->m_tabs.size(), 2);
    QTRY_VERIFY_WITH_TIMEOUT(shell->statusBar()->currentMessage().contains(QStringLiteral("1 document opened; 2 were already open")), 3000);
    QVERIFY(shell->statusBar()->isVisible());
}

void MainShellTest::testForwardingRejectsOversizeRequest()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QLocalSocket socket;
    socket.connectToServer(ShellUtils::windowsTabOpenServerName(), QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(1000));
    QByteArray oversized;
    QDataStream stream(&oversized, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << quint32(4 * 1024 * 1024 + 1);
    QCOMPARE(socket.write(oversized), oversized.size());
    socket.flush();
    QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QLocalSocket::UnconnectedState, 1000);
    QVERIFY(shell->m_tabs.first().part->url().isEmpty());
}

void MainShellTest::testShortcutLaunchReusesExistingWindow()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QVERIFY(shell->m_windowsTabOpenServer);

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("OKULAR_TEST_INSTANCE"), QString::number(QCoreApplication::applicationPid()));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(OKULAR_BINARY));
    QVERIFY(process.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 20000);
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(findShell(shell) == nullptr);
}

void MainShellTest::testAdditionalShellDoesNotRegisterWindowsTabOpenServer()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *primary = findShell();
    QVERIFY(primary);
    QVERIFY(primary->m_windowsTabOpenServer);
    QVERIFY(primary->m_windowsTabOpenServer->isListening());

    Shell *additional = new Shell(options);
    QVERIFY(additional->isValid());
    QVERIFY(!additional->m_windowsTabOpenServer);
    QVERIFY(primary->m_windowsTabOpenServer->isListening());
    delete additional;
}

void MainShellTest::testSeparateWindowSessionsRestore()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    const bool originalExplicitPalette = first->testAttribute(Qt::WA_SetPalette);
    first->openUrl(firstUrl);
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    second->show();
    second->resize(640, 480);
    second->openUrl(secondUrl);
    // Each window may have its own chrome, even though new windows share
    // defaults from the initial window.
    if (first->m_lockSidebarAction->isChecked()) {
        first->m_lockSidebarAction->trigger();
    }
    second->m_showSidebarAction->setChecked(false);
    second->m_sidebar->hide();
    second->m_fullScreenAction->trigger();
    QVERIFY(second->isFullScreen());
    second->saveOpenDocumentSession();

    KSharedConfigPtr config = testShellSessionConfig();
    const QStringList ids = Shell::savedOpenDocumentSessionWindowIds();
    QCOMPARE(ids.size(), 2);
    QCOMPARE(config->group(QStringLiteral("Shell Open Documents Session Window ") + ids.at(0)).readPathEntry("Urls", QStringList()).size(), 1);
    QCOMPARE(config->group(QStringLiteral("Shell Open Documents Session Window ") + ids.at(1)).readPathEntry("Urls", QStringList()).size(), 1);
    const QByteArray secondGeometry = config->group(QStringLiteral("Shell Open Documents Session Window ") + ids.at(1)).readEntry("WindowGeometry", QByteArray());
    QVERIFY(!secondGeometry.isEmpty());
    const KConfigGroup firstGroup = config->group(QStringLiteral("Shell Open Documents Session Window ") + ids.at(0));
    const KConfigGroup secondGroup = config->group(QStringLiteral("Shell Open Documents Session Window ") + ids.at(1));
    QCOMPARE(firstGroup.readEntry("WindowUiStateVersion", 0), 1);
    QCOMPARE(firstGroup.readEntry("WindowSidebarLocked", true), false);
    QCOMPARE(secondGroup.readEntry("WindowSidebarVisible", true), false);
    QCOMPARE(secondGroup.readEntry("WindowFullScreen", false), true);

    delete second;
    delete first;
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    const QList<Shell *> restored = getShells();
    QCOMPARE(restored.size(), 2);
    QVERIFY(restored.at(0)->updatesEnabled());
    QVERIFY(restored.at(1)->updatesEnabled());
    QCOMPARE(restored.at(0)->testAttribute(Qt::WA_SetPalette), originalExplicitPalette);
    QVERIFY(restored.at(0)->centralWidget()->isVisible());
    QCOMPARE(QDir::cleanPath(restored.at(0)->m_tabs.first().part->url().toLocalFile()), QDir::cleanPath(firstUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(restored.at(1)->m_tabs.first().part->url().toLocalFile()), QDir::cleanPath(secondUrl.toLocalFile()));
    QCOMPARE(restored.at(0)->m_lockSidebarAction->isChecked(), false);
    QCOMPARE(restored.at(1)->m_showSidebarAction->isChecked(), false);
    QVERIFY(restored.at(1)->isFullScreen());
    QVERIFY(restored.at(1)->m_fullScreenAction->isChecked());
    if (!restored.at(1)->isFullScreen()) {
        QCOMPARE(restored.at(1)->geometry().height(), 480);
    }
    if (QGuiApplication::platformName() != QLatin1String("offscreen") && !restored.at(1)->isFullScreen()) {
        QVERIFY(restored.at(1)->geometry().width() < restored.at(0)->geometry().width());
    }
}

void MainShellTest::testColorSchemeMenuTracksOtherWindow()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    Shell *third = new Shell(options, QString(), true);
    QVERIFY(third->isValid());

    QAction *firstMenuAction = first->m_tabs.first().part->actionCollection()->action(QStringLiteral("colorscheme_menu"));
    QAction *secondMenuAction = second->m_tabs.first().part->actionCollection()->action(QStringLiteral("colorscheme_menu"));
    QAction *thirdMenuAction = third->m_tabs.first().part->actionCollection()->action(QStringLiteral("colorscheme_menu"));
    QVERIFY(firstMenuAction);
    QVERIFY(secondMenuAction);
    QVERIFY(thirdMenuAction);
    QMenu *firstMenu = firstMenuAction->menu();
    QMenu *secondMenu = secondMenuAction->menu();
    QMenu *thirdMenu = thirdMenuAction->menu();
    QVERIFY(firstMenu);
    QVERIFY(secondMenu);
    QVERIFY(thirdMenu);
    const QList<QAction *> firstActions = firstMenu->actions();
    QList<QAction *> candidates;
    for (QAction *action : firstActions) {
        if (action->isCheckable() && !action->data().toString().isEmpty()) {
            candidates.append(action);
        }
    }
    if (candidates.size() < 2) {
        QSKIP("Two named color schemes are needed for this test");
    }
    const auto expectMenuSelection = [](QMenu *menu, const QString &selectedName) {
        menu->popup(QPoint(0, 0));
        QCoreApplication::processEvents();
        for (QAction *action : menu->actions()) {
            if (action->text() == selectedName) {
                const bool checked = action->isChecked();
                menu->hide();
                return checked;
            }
        }
        menu->hide();
        return false;
    };
    candidates.at(0)->trigger();
    QVERIFY(expectMenuSelection(secondMenu, candidates.at(0)->text()));
    QVERIFY(expectMenuSelection(thirdMenu, candidates.at(0)->text()));
    const QString secondName = candidates.at(1)->text();
    for (QAction *action : secondMenu->actions()) {
        if (action->text() == secondName) {
            action->trigger();
            break;
        }
    }
    QVERIFY(expectMenuSelection(firstMenu, secondName));
    QVERIFY(expectMenuSelection(thirdMenu, secondName));
}

#if defined(Q_OS_WIN)
void MainShellTest::testPageNumberControlWidthStaysStable()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    const auto pageControlWidth = [shell](const QString &path) {
        shell->openUrl(QUrl::fromLocalFile(path));
        QCoreApplication::processEvents();
        QWidget *bar = shell->m_tabs.at(shell->m_tabWidget->currentIndex()).part->widget()->findChild<QWidget *>(QStringLiteral("miniBar"));
        if (!bar) {
            return -1;
        }
        QWidget *edit = bar->findChild<QWidget *>(QStringLiteral("pageNumberEdit"));
        return edit ? edit->width() : -1;
    };

    const int singlePageWidth = pageControlWidth(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    QVERIFY(singlePageWidth > 0);
    const int multipageWidth = pageControlWidth(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf"));
    QCOMPARE(multipageWidth, singlePageWidth);
    QVERIFY(singlePageWidth < shell->fontMetrics().horizontalAdvance(QStringLiteral("8888888")) + 14);

    QWidget *bar = shell->m_tabs.at(shell->m_tabWidget->currentIndex()).part->widget()->findChild<QWidget *>(QStringLiteral("miniBar"));
    QVERIFY(bar);
    QLineEdit *edit = bar->findChild<QLineEdit *>(QStringLiteral("pageNumberEdit"));
    QVERIFY(edit);
    QCOMPARE(edit->maxLength(), 2); // simple-multipage.pdf has 40 pages
    edit->clear();
    QTest::keyClicks(edit, QStringLiteral("000000"));
    QCOMPARE(edit->text(), QString());
    QTest::keyClicks(edit, QStringLiteral("200000"));
    QCOMPARE(edit->text(), QStringLiteral("20"));
}

void MainShellTest::testTabWidthsStayStableAcrossSelection()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QTabBar *bar = shell->m_tabWidget->tabBar();

    int roomyWidth = 0;
    int compactWidth = 0;
    for (int count = 1; count <= 6; ++count) {
        const QString path = directory.filePath(QStringLiteral("document-%1.pdf").arg(count));
        QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), path));
        shell->openUrl(QUrl::fromLocalFile(path));
        QCoreApplication::processEvents();
        QCOMPARE(bar->count(), count);
        const int width = bar->tabRect(0).width();
        QList<int> closeButtonOffsets;
        for (int selected = 0; selected < count; ++selected) {
            shell->setActiveTab(selected);
            QCoreApplication::processEvents();
            for (int tab = 0; tab < count; ++tab) {
                QCOMPARE(bar->tabRect(tab).width(), width);
                QWidget *closeButton = bar->tabButton(tab, QTabBar::RightSide);
                QVERIFY(closeButton);
                const int offset = closeButton->geometry().x() - bar->tabRect(tab).x();
                if (selected == 0) {
                    closeButtonOffsets.append(offset);
                } else {
                    QCOMPARE(offset, closeButtonOffsets.at(tab));
                }
            }
        }
        if (count == 1) {
            roomyWidth = width;
            QVERIFY(roomyWidth >= 220);
        } else if (count < 5) {
            QCOMPARE(width, roomyWidth);
        } else if (count == 5) {
            compactWidth = width;
            QVERIFY(compactWidth < roomyWidth);
        } else {
            QCOMPARE(width, compactWidth);
        }
    }

    QVERIFY(shell->closeTab(5));
    QCoreApplication::processEvents();
    QCOMPARE(bar->count(), 5);
    QCOMPARE(bar->tabRect(0).width(), compactWidth);
    QVERIFY(shell->closeTab(4));
    QCoreApplication::processEvents();
    QCOMPARE(bar->count(), 4);
    QCOMPARE(bar->tabRect(0).width(), roomyWidth);
    delete shell;
}

void MainShellTest::testTabPaletteTracksColorScheme()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    QCoreApplication::processEvents();
    QTabBar *bar = shell->m_tabWidget->tabBar();
    QCOMPARE(bar->count(), 1);
    QCOMPARE(bar->currentIndex(), 0);
    const QPalette original = qApp->palette();
    const auto restorePalette = qScopeGuard([&]() { qApp->setPalette(original); });
    const auto selectedTabHasAccent = [bar](const QColor &accent) {
        const QImage pixels = bar->grab().toImage();
        const QRect tab = bar->tabRect(bar->currentIndex());
        for (int y = tab.top(); y < qMin(tab.top() + 4, pixels.height()); ++y) {
            for (int x = tab.left() + 8; x < qMin(tab.right() - 8, pixels.width()); ++x) {
                const QColor pixel = pixels.pixelColor(x, y);
                if (qAbs(pixel.red() - accent.red()) <= 8 && qAbs(pixel.green() - accent.green()) <= 8 && qAbs(pixel.blue() - accent.blue()) <= 8) {
                    return true;
                }
            }
        }
        return false;
    };

    QPalette dark = original;
    dark.setColor(QPalette::Window, QColor(24, 27, 30));
    dark.setColor(QPalette::Button, QColor(34, 37, 41));
    dark.setColor(QPalette::Base, QColor(42, 42, 42));
    dark.setColor(QPalette::WindowText, QColor(220, 224, 228));
    dark.setColor(QPalette::Highlight, QColor(90, 160, 210));
    qApp->setPalette(dark);
    QCoreApplication::processEvents();
    QVERIFY(shell->m_tabWidget->tabBar()->styleSheet().contains(QStringLiteral("background-color: #222529")));
    QVERIFY(shell->m_tabWidget->tabBar()->styleSheet().contains(QStringLiteral("color: #dce0e4")));
    QVERIFY(shell->m_tabWidget->styleSheet().contains(QStringLiteral("background: #181b1e")));
    QCOMPARE(shell->m_tabWidget->cornerWidget(Qt::TopRightCorner)->palette().color(QPalette::Window), QColor(24, 27, 30));
    QVERIFY(selectedTabHasAccent(QColor(90, 160, 210)));
    QVERIFY(shell->m_tabWidget->cornerWidget(Qt::TopRightCorner)->rect().contains(shell->m_openTabButton->geometry()));
    shell->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")));
    QCOMPARE(bar->count(), 2);
    shell->setActiveTab(0);
    QList<int> darkCloseOffsets;
    for (int tab = 0; tab < 2; ++tab) {
        QWidget *closeButton = bar->tabButton(tab, QTabBar::RightSide);
        QVERIFY(closeButton);
        darkCloseOffsets.append(closeButton->geometry().x() - bar->tabRect(tab).x());
    }
    shell->setActiveTab(1);
    QCoreApplication::processEvents();
    for (int tab = 0; tab < 2; ++tab) {
        QCOMPARE(bar->tabButton(tab, QTabBar::RightSide)->geometry().x() - bar->tabRect(tab).x(), darkCloseOffsets.at(tab));
    }
    QVERIFY(selectedTabHasAccent(QColor(90, 160, 210)));
    QVERIFY(shell->closeTab(1));
    QCoreApplication::processEvents();
    QCOMPARE(bar->count(), 1);
    QCOMPARE(bar->currentIndex(), 0);
    QVERIFY(selectedTabHasAccent(QColor(90, 160, 210)));

    QPalette light = original;
    light.setColor(QPalette::Window, QColor(235, 239, 242));
    light.setColor(QPalette::Highlight, QColor(68, 150, 195));
    qApp->setPalette(light);
    QCoreApplication::processEvents();
    QVERIFY(shell->m_tabWidget->styleSheet().contains(QStringLiteral("background: #ebeff2")));
    QVERIFY(!shell->m_tabWidget->tabBar()->styleSheet().contains(QStringLiteral("background-color: #222529")));
    QVERIFY(selectedTabHasAccent(QColor(68, 150, 195)));
    QVERIFY(shell->m_tabWidget->cornerWidget(Qt::TopRightCorner)->rect().contains(shell->m_openTabButton->geometry()));

}

void MainShellTest::testDuplicateOpenShowsPageNotice()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    const QUrl url = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    shell->openUrl(url);
    QWidget *notice = shell->m_tabs.first().part->widget()->findChild<QWidget *>(QStringLiteral("pageViewMessage"));
    QVERIFY(notice);
    const bool oldShowOSD = Okular::Settings::showOSD();
    const auto restoreSetting = qScopeGuard([&]() { Okular::Settings::self()->setShowOSD(oldShowOSD); });
    Okular::Settings::self()->setShowOSD(true);

    notice->hide();
    shell->openDocumentInTab(url, QString()); // File > Open and the + button
    QCOMPARE(shell->m_tabs.size(), 1);
    QVERIFY(notice->isVisible());

    notice->hide();
    shell->openUrl(url); // ordinary open, including a forwarded file
    QCOMPARE(shell->m_tabs.size(), 1);
    QVERIFY(notice->isVisible());

    notice->hide();
    Okular::Settings::self()->setShowOSD(false);
    shell->openDocumentInTab(url, QString());
    QVERIFY(!notice->isVisible());

    Okular::Settings::self()->setShowOSD(true);
    Shell *other = new Shell(options, QString(), true);
    QVERIFY(other->isValid());
    other->show();
    const QUrl otherUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    other->openUrl(otherUrl);
    QWidget *otherNotice = other->m_tabs.first().part->widget()->findChild<QWidget *>(QStringLiteral("pageViewMessage"));
    QVERIFY(otherNotice);
    otherNotice->hide();
    shell->openDocumentInTab(otherUrl, QString());
    QCOMPARE(shell->m_tabs.size(), 1);
    QCOMPARE(other->m_tabs.size(), 1);
    QVERIFY(otherNotice->isVisible());
}
#endif

void MainShellTest::testSharedViewerSettingsAcrossWindows()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    second->show();
    first->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    second->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")));
    auto *firstPart = qobject_cast<Okular::Part *>(first->m_tabs.first().part);
    auto *secondPart = qobject_cast<Okular::Part *>(second->m_tabs.first().part);
    QVERIFY(firstPart && secondPart);
    QAction *sidebarAction = firstPart->actionCollection()->action(QStringLiteral("show_leftpanel"));
    QAction *otherSidebarAction = secondPart->actionCollection()->action(QStringLiteral("show_leftpanel"));
    QAction *bottomAction = firstPart->actionCollection()->action(QStringLiteral("show_bottombar"));
    QAction *otherBottomAction = secondPart->actionCollection()->action(QStringLiteral("show_bottombar"));
    QAction *facingAction = firstPart->actionCollection()->action(QStringLiteral("view_render_mode_facing"));
    QAction *otherFacingAction = secondPart->actionCollection()->action(QStringLiteral("view_render_mode_facing"));
    QAction *singleAction = firstPart->actionCollection()->action(QStringLiteral("view_render_mode_single"));
    QAction *otherSingleAction = secondPart->actionCollection()->action(QStringLiteral("view_render_mode_single"));
    QVERIFY(sidebarAction && otherSidebarAction && bottomAction && otherBottomAction && facingAction && otherFacingAction && singleAction && otherSingleAction);

    sidebarAction->trigger();
    QTRY_COMPARE(otherSidebarAction->isChecked(), sidebarAction->isChecked());
    QCOMPARE(sidebar(secondPart)->isSidebarVisible(), sidebarAction->isChecked());
    bottomAction->trigger();
    QTRY_COMPARE(otherBottomAction->isChecked(), bottomAction->isChecked());
    QCOMPARE(bottomBar(secondPart)->isVisible(), bottomAction->isChecked());
    singleAction->trigger();
    otherSingleAction->trigger();
    QVERIFY(otherSingleAction->isChecked());
    facingAction->trigger();
    // The action is per document, while the last selected mode is the shared
    // default. This does not by itself prove the two documents render differently.
    QVERIFY(!otherFacingAction->isChecked());
    QCOMPARE(pageView(firstPart)->capability(Okular::View::ViewModeModality).toInt(), int(Okular::Settings::EnumViewMode::Facing));
    QCOMPARE(pageView(secondPart)->capability(Okular::View::ViewModeModality).toInt(), int(Okular::Settings::EnumViewMode::Single));
    QCOMPARE(Okular::Settings::viewMode(), int(Okular::Settings::EnumViewMode::Facing));
    pageView(secondPart)->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Summary));
    QCOMPARE(pageView(secondPart)->capability(Okular::View::ViewModeModality).toInt(), int(Okular::Settings::EnumViewMode::Summary));
    QCOMPARE(pageView(firstPart)->capability(Okular::View::ViewModeModality).toInt(), int(Okular::Settings::EnumViewMode::Facing));
    QCOMPARE(Okular::Settings::viewMode(), int(Okular::Settings::EnumViewMode::Facing));
    otherFacingAction->trigger();
    QVERIFY(otherFacingAction->isChecked());

    Okular::Settings::setShowScrollBars(false);
    Okular::Settings::self()->save();
    QTRY_COMPARE(pageView(firstPart)->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QTRY_COMPARE(pageView(secondPart)->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QAction *rtlAction = firstPart->actionCollection()->action(QStringLiteral("rtl_page_layout"));
    QAction *otherRtlAction = secondPart->actionCollection()->action(QStringLiteral("rtl_page_layout"));
    QVERIFY(rtlAction && otherRtlAction);
    rtlAction->trigger();
    QTRY_COMPARE(otherRtlAction->isChecked(), rtlAction->isChecked());
}

void MainShellTest::testSavedViewModeDoesNotChangeOtherWindow()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    second->show();
    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    first->openUrl(firstUrl);
    second->openUrl(secondUrl);
    auto *firstPart = qobject_cast<Okular::Part *>(first->m_tabs.first().part);
    auto *secondPart = qobject_cast<Okular::Part *>(second->m_tabs.first().part);
    QVERIFY(firstPart && secondPart);
    QAction *facing = firstPart->actionCollection()->action(QStringLiteral("view_render_mode_facing"));
    QAction *single = secondPart->actionCollection()->action(QStringLiteral("view_render_mode_single"));
    QVERIFY(facing && single);
    facing->trigger();
    single->trigger();
    QCOMPARE(Okular::Settings::viewMode(), int(Okular::Settings::EnumViewMode::Single));
    QVERIFY(firstPart->closeUrl());
    firstPart->openDocument(firstUrl.toLocalFile());
    QVERIFY(partDocument(firstPart)->isOpened());
    QVERIFY(facing->isChecked());
    QCOMPARE(Okular::Settings::viewMode(), int(Okular::Settings::EnumViewMode::Single));
    QVERIFY(single->isChecked());
}

void MainShellTest::testClosingTabsKeepsSinglePageCentered()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->show();
    shell->resize(800, 700);
    QCoreApplication::processEvents();
    const QStringList files = {QStringLiteral(KDESRCDIR "data/file1.pdf"), QStringLiteral(KDESRCDIR "data/file2.pdf"), QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")};
    for (const QString &file : files) {
        shell->openUrl(QUrl::fromLocalFile(file));
        auto *part = qobject_cast<Okular::Part *>(shell->m_tabs.at(shell->m_tabWidget->currentIndex()).part);
        pageView(part)->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Single));
        pageView(part)->setCapability(Okular::View::Zoom, 1.0);
    }
    shell->m_sidebar->hide();
    shell->showMaximized();
    QCoreApplication::processEvents();
    shell->closeUrl();
    QCOMPARE(shell->m_tabs.size(), 2);
    auto *nextPart = qobject_cast<Okular::Part *>(shell->m_tabs.at(shell->m_tabWidget->currentIndex()).part);
    auto *view = pageView(nextPart);
    const QImage firstFrame = view->viewport()->grab().toImage();
    auto *resizeTimer = view->findChild<QTimer *>(QStringLiteral("delayResizeEventTimer"));
    QVERIFY(resizeTimer);
    QVERIFY(!resizeTimer->isActive()); // the revealed page was laid out before its first paint
    if (firstFrame.width() >= 1500) {
        const int y = firstFrame.height() / 2;
        int left = -1;
        int right = -1;
        for (int x = 0; x < firstFrame.width(); ++x) {
            const QColor color = firstFrame.pixelColor(x, y);
            if (color.red() > 245 && color.green() > 245 && color.blue() > 245) {
                if (left < 0) {
                    left = x;
                }
                right = x;
            }
        }
        QVERIFY(left > 0);
        QVERIFY(right < firstFrame.width() - 1);
        QVERIFY(qAbs((left + right) / 2 - firstFrame.width() / 2) < 16);
    }
    shell->closeUrl();
    QCOMPARE(shell->m_tabs.size(), 1);
    nextPart = qobject_cast<Okular::Part *>(shell->m_tabs.at(shell->m_tabWidget->currentIndex()).part);
    view = pageView(nextPart);
    view->viewport()->grab();
    resizeTimer = view->findChild<QTimer *>(QStringLiteral("delayResizeEventTimer"));
    QVERIFY(resizeTimer);
    QVERIFY(!resizeTimer->isActive());
}

void MainShellTest::testSharedReviewAndLayerSettingsAcrossWindows()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    second->show();
    auto *firstPart = qobject_cast<Okular::Part *>(first->m_tabs.first().part);
    auto *secondPart = qobject_cast<Okular::Part *>(second->m_tabs.first().part);
    QVERIFY(firstPart && secondPart);
    QToolBar *firstBar = reviews(firstPart)->findChild<QToolBar *>(QStringLiteral("reviewOptsBar"));
    QToolBar *secondBar = reviews(secondPart)->findChild<QToolBar *>(QStringLiteral("reviewOptsBar"));
    QVERIFY(firstBar && secondBar);
    const int actionIndexes[] = {0, 1, 3};
    for (int index : actionIndexes) {
        QAction *action = firstBar->actions().at(index);
        QAction *otherAction = secondBar->actions().at(index);
        action->trigger();
        QTRY_COMPARE(otherAction->isChecked(), action->isChecked());
    }
    KConfigGroup navPanel = KSharedConfig::openConfig(QStringLiteral("mainshelltest-settingsrc"))->group(QStringLiteral("Nav Panel"));
    QCOMPARE(navPanel.readEntry("GroupByPage", true), firstBar->actions().at(0)->isChecked());
    QCOMPARE(navPanel.readEntry("GroupByAuthor", true), firstBar->actions().at(1)->isChecked());
    QCOMPARE(navPanel.readEntry("CurrentPageOnly", false), firstBar->actions().at(3)->isChecked());

    const auto layerSearch = [this](Okular::Part *part) -> QWidget * {
        for (QWidget *widget : layers(part)->findChildren<QWidget *>()) {
            if (widget->inherits("KTreeViewSearchLine")) {
                return widget;
            }
        }
        return nullptr;
    };
    QWidget *firstSearch = layerSearch(firstPart);
    QWidget *secondSearch = layerSearch(secondPart);
    QVERIFY(firstSearch && secondSearch);
    QVERIFY(firstSearch->setProperty("caseSensitity", int(Qt::CaseSensitive)));
    QTRY_COMPARE(secondSearch->property("caseSensitity").toInt(), int(Qt::CaseSensitive));
    QVERIFY(QMetaObject::invokeMethod(firstSearch, "setRegularExpression", Q_ARG(bool, true)));
    QTRY_VERIFY(Okular::Settings::layersSearchRegularExpression());
}

void MainShellTest::testRecentlyClosedWindowRestoresRemainingTabs()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    const QUrl closedTab = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl retainedTab = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    first->openUrl(closedTab);
    first->openUrl(retainedTab);
    first->closeTab(0);
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    second->show();
    second->openUrl(closedTab);
    second->saveOpenDocumentSession();
    const QStringList ids = Shell::savedOpenDocumentSessionWindowIds();
    QCOMPARE(ids.size(), 2);

    QVERIFY(first->close());
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), ids);
    const KConfigGroup closedGroup = KSharedConfig::openConfig()->group(QStringLiteral("Shell Open Documents Session Window ") + ids.first());
    const QStringList savedUrls = closedGroup.readPathEntry("Urls", QStringList());
    QCOMPARE(savedUrls.size(), 1);
    QCOMPARE(QDir::cleanPath(QUrl(savedUrls.first()).toLocalFile()), QDir::cleanPath(retainedTab.toLocalFile()));
    QVERIFY(closedGroup.readEntry("ClosedAt", qint64(0)) > 0);
    delete first;
    QVERIFY(second->close());
    delete second;

    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    const QList<Shell *> restored = getShells();
    QCOMPARE(restored.size(), 2);
    QCOMPARE(QDir::cleanPath(restored.at(0)->m_tabs.first().part->url().toLocalFile()), QDir::cleanPath(retainedTab.toLocalFile()));
    QCOMPARE(QDir::cleanPath(restored.at(1)->m_tabs.first().part->url().toLocalFile()), QDir::cleanPath(closedTab.toLocalFile()));
}

void MainShellTest::testCloseAllHiddenWindowsRestoresThreeSessions()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")),
    };
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    first->openUrl(urls.at(0));
    Shell *second = new Shell(options, QString(), true);
    Shell *third = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    QVERIFY(third->isValid());
    second->show();
    third->show();
    second->openUrl(urls.at(1));
    third->openUrl(urls.at(2));
    third->saveOpenDocumentSession();
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds().size(), 3);

    // Windows taskbar "Close all windows" may hide every window before its
    // queued close events have been handled.
    first->hide();
    second->hide();
    third->hide();
    QVERIFY(first->close());
    QVERIFY(second->close());
    QVERIFY(third->close());
    const QStringList savedIds = Shell::savedOpenDocumentSessionWindowIds();
    QCOMPARE(savedIds.size(), 3);
    for (const QString &id : savedIds) {
        QCOMPARE(KSharedConfig::openConfig()->group(QStringLiteral("Shell Open Documents Session Window ") + id).readEntry("ClosedAt", qint64(0)), qint64(0));
    }
    delete first;
    delete second;
    delete third;

    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    const QList<Shell *> restored = getShells();
    QCOMPARE(restored.size(), 3);
    for (int i = 0; i < restored.size(); ++i) {
        QCOMPARE(QDir::cleanPath(restored.at(i)->m_tabs.first().part->url().toLocalFile()), QDir::cleanPath(urls.at(i).toLocalFile()));
    }
}

void MainShellTest::testCloseAllAfterClosingSomeTabsRestoresRemainingTabs()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/tocreload.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/visibilitytest.pdf")),
    };
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    first->openUrl(urls.at(0));
    Shell *second = new Shell(options, QString(), true);
    Shell *third = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    QVERIFY(third->isValid());
    second->show();
    third->show();
    second->openUrl(urls.at(1));
    for (const QUrl &url : urls.mid(2)) {
        third->openUrl(url);
    }
    QVERIFY(third->closeTab(0));
    QVERIFY(third->closeTab(0));
    QCOMPARE(third->m_tabs.size(), 3);
    third->saveOpenDocumentSession();

    for (Shell *window : {first, second, third}) {
        window->hide();
    }
    KMessageBox::saveDontShowAgainTwoActions(QStringLiteral("ShowTabWarning"), KMessageBox::PrimaryAction);
    for (Shell *window : {first, second, third}) {
        QVERIFY(window->close());
    }
    delete first;
    delete second;
    delete third;

    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    const QList<Shell *> restored = getShells();
    QCOMPARE(restored.size(), 3);
    QCOMPARE(restored.at(2)->m_tabs.size(), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(QDir::cleanPath(restored.at(2)->m_tabs.at(i).part->url().toLocalFile()), QDir::cleanPath(urls.at(i + 4).toLocalFile()));
    }
}

void MainShellTest::testFiveWindowShutdownSurvivesPartialRestoreSave()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/tocreload.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")),
    };
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    QList<Shell *> windows {findShell()};
    QVERIFY(windows.first());
    windows.first()->openUrl(urls.first());
    for (int i = 1; i < urls.size(); ++i) {
        Shell *window = new Shell(options, QString(), true);
        QVERIFY(window->isValid());
        window->show();
        window->openUrl(urls.at(i));
        windows.append(window);
    }
    windows.last()->saveOpenDocumentSession();
    const QStringList originalIds = Shell::savedOpenDocumentSessionWindowIds();
    QCOMPARE(originalIds.size(), 5);

    // Two ordinary closes, then explicitly remove the third window's only tab.
    for (int i = 0; i < 2; ++i) {
        QVERIFY(windows.at(i)->close());
        delete windows[i];
    }
    QVERIFY(windows.at(2)->closeTab(0));
    QVERIFY(windows.at(2)->close());
    delete windows[2];

    // The taskbar can hide both remaining windows before either receives a close event.
    for (int i = 3; i < 5; ++i) {
        windows.at(i)->hide();
    }
    for (int i = 3; i < 5; ++i) {
        QVERIFY(windows.at(i)->close());
        delete windows[i];
    }
    const QStringList expectedIds {originalIds.at(0), originalIds.at(1), originalIds.at(3), originalIds.at(4)};
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), expectedIds);

    // A save from the first restored window must not discard windows that the
    // startup loop has not constructed yet, even if a timer fires mid-restore.
    Shell::setWindowsSessionRestoreInProgress(true);
    const auto resetRestoreFlag = qScopeGuard([]() { Shell::setWindowsSessionRestoreInProgress(false); });
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), expectedIds);
    // A concurrent settings write during startup must not change the windows
    // or tab URLs selected at the beginning of this restore.
    KSharedConfigPtr config = KSharedConfig::openConfig();
    config->group(QStringLiteral("Shell Open Documents Session Window ") + expectedIds.last()).writePathEntry("Urls", QStringList());
    config->sync();
    Shell *first = new Shell(options, expectedIds.first(), false);
    QVERIFY(first->isValid());
    first->show();
    QVERIFY(first->restoreOpenDocumentSession());
    first->saveOpenDocumentSession();
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), expectedIds);
    for (const QString &id : expectedIds.mid(1)) {
        Shell *additional = new Shell(options, id, true);
        QVERIFY(additional->isValid());
        additional->show();
        QVERIFY(additional->restoreOpenDocumentSession());
    }
    Shell::setWindowsSessionRestoreInProgress(false);
    first->saveOpenDocumentSession();
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), expectedIds);
    QCOMPARE(getShells().size(), 4);
}

void MainShellTest::testSequentialFiveWindowCloseRestoresAll()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    QList<Shell *> windows {findShell()};
    QVERIFY(windows.first());
    const QStringList files {
        QStringLiteral(KDESRCDIR "data/file1.pdf"),
        QStringLiteral(KDESRCDIR "data/file2.pdf"),
        QStringLiteral(KDESRCDIR "data/simple-multipage.pdf"),
        QStringLiteral(KDESRCDIR "data/tocreload.pdf"),
        QStringLiteral(KDESRCDIR "data/file1.pdf"),
    };
    windows.first()->openUrl(QUrl::fromLocalFile(files.first()));
    for (const QString &file : files.mid(1)) {
        Shell *window = new Shell(options, QString(), true);
        QVERIFY(window->isValid());
        window->show();
        window->openUrl(QUrl::fromLocalFile(file));
        windows.append(window);
    }
    windows.last()->saveOpenDocumentSession();
    const QStringList expectedIds = Shell::savedOpenDocumentSessionWindowIds();
    QCOMPARE(expectedIds.size(), 5);
    for (Shell *window : std::as_const(windows)) {
        QVERIFY(window->close());
        delete window;
        QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), expectedIds);
    }
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    QCOMPARE(getShells().size(), 5);
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), expectedIds);
}

void MainShellTest::testPrimaryTakeoverKeepsPreviousOwnersWindows()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    shell->saveOpenDocumentSession();
    const QStringList cachedIds = Shell::savedOpenDocumentSessionWindowIds();
    QCOMPARE(cachedIds.size(), 1);

    const KSharedConfigPtr cached = KSharedConfig::openConfig();
    KConfig previousOwner(cached->name(), KConfig::SimpleConfig);
    const QString previousId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    previousOwner.group(QStringLiteral("Shell Open Documents Session Window ") + previousId)
        .writePathEntry("Urls", QStringList {QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")).url()});
    previousOwner.group(QStringLiteral("Shell Open Documents Session Windows"))
        .writeEntry("WindowIds", QStringList {cachedIds.first(), previousId});
    previousOwner.sync();
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), cachedIds);

    shell->mergeWindowSessionsWrittenByPreviousOwner();
    shell->saveOpenDocumentSession();
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds().size(), 2);
    delete shell;
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    QCOMPARE(getShells().size(), 2);
}

void MainShellTest::testStartupReadsPreviousOwnersFinalSession()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    shell->saveOpenDocumentSession();
    const QString firstId = Shell::savedOpenDocumentSessionWindowIds().first();
    delete shell;

    const KSharedConfigPtr cached = KSharedConfig::openConfig();
    KConfig previousOwner(cached->name(), KConfig::SimpleConfig);
    const QString secondId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    previousOwner.group(QStringLiteral("Shell Open Documents Session Window ") + secondId)
        .writePathEntry("Urls", QStringList {QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")).url()});
    previousOwner.group(QStringLiteral("Shell Open Documents Session Windows"))
        .writeEntry("WindowIds", QStringList {firstId, secondId});
    previousOwner.sync();
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), QStringList {firstId});

    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    QCOMPARE(getShells().size(), 2);
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), (QStringList {firstId, secondId}));
}

void MainShellTest::testClosedWindowsCannotOverwriteCompletedShutdown()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    QList<Shell *> windows {findShell()};
    QVERIFY(windows.first());
    const QList<QUrl> urls {
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")),
    };
    windows.first()->openUrl(urls.first());
    for (const QUrl &url : urls.mid(1)) {
        Shell *window = new Shell(options, QString(), true);
        QVERIFY(window->isValid());
        window->show();
        window->openUrl(url);
        windows.append(window);
    }
    windows.last()->saveOpenDocumentSession();
    const QStringList ids = Shell::savedOpenDocumentSessionWindowIds();
    QCOMPARE(ids.size(), 3);

    for (Shell *window : std::as_const(windows)) {
        window->hide();
    }
    for (Shell *window : std::as_const(windows)) {
        QVERIFY(window->close());
    }
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), ids);
    // A queued timer or signal can still address a closed Shell before Qt
    // destroys it. None of those saves may replace the completed session.
    for (Shell *window : std::as_const(windows)) {
        QVERIFY(window->m_sessionClosed);
        QVERIFY(!window->m_openDocumentSessionSaveTimer->isActive());
        window->saveOpenDocumentSession();
    }
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), ids);
    for (const QString &id : ids) {
        QVERIFY(!KSharedConfig::openConfig()->group(QStringLiteral("Shell Open Documents Session Window ") + id).readPathEntry("Urls", QStringList()).isEmpty());
    }
}

void MainShellTest::testEmptyFirstWindowDoesNotRestore()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    first->openUrl(firstUrl);
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    second->show();
    second->openUrl(secondUrl);
    second->saveOpenDocumentSession();
    QVERIFY(first->closeTab(0));
    QVERIFY(first->m_tabs.first().part->url().isEmpty());
    QVERIFY(second->close());
    QVERIFY(first->close());
    delete second;
    delete first;

    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    const QList<Shell *> restored = getShells();
    QCOMPARE(restored.size(), 1);
    QCOMPARE(QDir::cleanPath(restored.first()->m_tabs.first().part->url().toLocalFile()), QDir::cleanPath(secondUrl.toLocalFile()));
}

void MainShellTest::testTemporarilyMissingTabSurvivesRestore()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString intermittentPath = temporary.filePath(QStringLiteral("intermittent.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file2.pdf"), intermittentPath));
    const QUrl stableUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl intermittentUrl = QUrl::fromLocalFile(intermittentPath);
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->openUrl(stableUrl);
    shell->openUrl(intermittentUrl);
    shell->saveOpenDocumentSession();
    const QString id = Shell::savedOpenDocumentSessionWindowIds().first();
    delete shell;

    QVERIFY(QFile::remove(intermittentPath));
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    shell = findShell();
    QVERIFY(shell);
    QCOMPARE(shell->m_tabs.size(), 1);
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("Shell Open Documents Session Window ") + id);
    const auto containsIntermittent = [&]() {
        const QStringList saved = group.readPathEntry("Urls", QStringList());
        return std::any_of(saved.cbegin(), saved.cend(), [&intermittentPath](const QString &entry) {
            return QDir::cleanPath(QUrl(entry).toLocalFile()) == QDir::cleanPath(intermittentPath);
        });
    };
    QVERIFY(containsIntermittent());
    delete shell;

    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file2.pdf"), intermittentPath));
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    shell = findShell();
    QVERIFY(shell);
    QCOMPARE(shell->m_tabs.size(), 2);
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(1).part->url().toLocalFile()), QDir::cleanPath(intermittentPath));
    QVERIFY(shell->closeTab(1));
    shell->saveOpenDocumentSession();
    QVERIFY(!containsIntermittent());
    delete shell;
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    QCOMPARE(findShell()->m_tabs.size(), 1);
}

void MainShellTest::testReopeningMissingTabClearsPendingEntry()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString intermittentPath = temporary.filePath(QStringLiteral("intermittent.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file2.pdf"), intermittentPath));
    const QUrl intermittentUrl = QUrl::fromLocalFile(intermittentPath);
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    shell->openUrl(intermittentUrl);
    shell->saveOpenDocumentSession();
    delete shell;

    QVERIFY(QFile::remove(intermittentPath));
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    shell = findShell();
    QVERIFY(shell);
    QCOMPARE(shell->m_tabs.size(), 1);
    QCOMPARE(shell->m_unrestoredSessionUrls.size(), 1);

    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file2.pdf"), intermittentPath));
    shell->openUrl(intermittentUrl);
    QCOMPARE(shell->m_tabs.size(), 2);
    QVERIFY(shell->m_unrestoredSessionUrls.isEmpty());
    shell->saveOpenDocumentSession();
    const QString id = Shell::savedOpenDocumentSessionWindowIds().first();
    const QStringList urls = KSharedConfig::openConfig()->group(QStringLiteral("Shell Open Documents Session Window ") + id).readPathEntry("Urls", QStringList());
    QCOMPARE(urls.size(), 2);
    delete shell;
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    QCOMPARE(findShell()->m_tabs.size(), 2);
}

void MainShellTest::testClosedWindowExpiresAfterFiveMinutes()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    first->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    second->show();
    second->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")));
    second->saveOpenDocumentSession();
    const QStringList ids = Shell::savedOpenDocumentSessionWindowIds();
    QCOMPARE(ids.size(), 2);

    QVERIFY(first->close());
    delete first;
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup closedGroup = config->group(QStringLiteral("Shell Open Documents Session Window ") + ids.first());
    closedGroup.writeEntry("ClosedAt", QDateTime::currentSecsSinceEpoch() - 4 * 60);
    config->sync();
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), ids);
    closedGroup.writeEntry("ClosedAt", QDateTime::currentSecsSinceEpoch() - 6 * 60);
    config->sync();
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), QStringList {ids.last()});
    QVERIFY(second->close());
    delete second;
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), QStringList {ids.last()});
}

void MainShellTest::testClockCorrectionKeepsRecentlyClosedWindow()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    first->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    second->show();
    second->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")));
    second->saveOpenDocumentSession();
    const QStringList ids = Shell::savedOpenDocumentSessionWindowIds();
    QCOMPARE(ids.size(), 2);

    QVERIFY(first->close());
    delete first;
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup firstGroup = config->group(QStringLiteral("Shell Open Documents Session Window ") + ids.first());
    firstGroup.writeEntry("ClosedAt", QDateTime::currentSecsSinceEpoch() + 60);
    config->sync();
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds(), ids);
    QVERIFY(second->close());
    delete second;
    QCOMPARE(firstGroup.readEntry("ClosedAt", qint64(0)), qint64(0));
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    QCOMPARE(getShells().size(), 2);
}

void MainShellTest::testClosingPrimaryTransfersForwarding()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *primary = findShell();
    QVERIFY(primary);
    Shell *additional = new Shell(options, QString(), true);
    QVERIFY(additional->isValid());
    additional->show();
    const QUrl url = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    additional->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")));
    QVERIFY(primary->close());
    QCOMPARE(additional->m_windowsTabOpenServer->isListening(), true);
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds().size(), 1);
    delete primary;

    QLocalSocket socket;
    socket.connectToServer(ShellUtils::windowsTabOpenServerName(), QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(1000));
    const QByteArray payload = tabOpenRequest(0x4f4b5450, options, {url.toLocalFile()});
    QCOMPARE(socket.write(payload), payload.size());
    socket.flush();
    QTRY_COMPARE(additional->m_tabs.at(additional->m_tabWidget->currentIndex()).part->url(), url);
    QVERIFY(socket.bytesAvailable() > 0 || socket.waitForReadyRead(1000));
    QCOMPARE(socket.read(1), QByteArrayLiteral("A"));
    additional->saveOpenDocumentSession();
    delete additional;
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *restored = findShell();
    QVERIFY(restored);
    QVERIFY(restored->m_intentionalSeparateWindow);
    QCOMPARE(QDir::cleanPath(restored->m_tabs.at(restored->m_tabWidget->currentIndex()).part->url().toLocalFile()), QDir::cleanPath(url.toLocalFile()));
}

void MainShellTest::testElevatedRequestDeclinesUnreadableFile()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missingPath = dir.filePath(QStringLiteral("missing.pdf"));
    QLocalSocket socket;
    socket.connectToServer(ShellUtils::windowsStandardTabOpenServerName(), QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(1000));
    ULONG serverPid = 0;
    QVERIFY(GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket.socketDescriptor()), &serverPid));
    QCOMPARE(serverPid, ULONG(QCoreApplication::applicationPid()));
    const QByteArray payload = tabOpenRequest(0x4f4b5445, options, {missingPath});
    QCOMPARE(socket.write(payload), payload.size());
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0, 1000);
    QCOMPARE(socket.read(1), QByteArrayLiteral("D"));
    QVERIFY(shell->m_tabs.first().part->url().isEmpty());

    QLocalSocket readableSocket;
    readableSocket.connectToServer(ShellUtils::windowsStandardTabOpenServerName(), QIODevice::ReadWrite);
    QVERIFY(readableSocket.waitForConnected(1000));
    const QUrl readableUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QByteArray readablePayload = tabOpenRequest(0x4f4b5445, options, {readableUrl.toLocalFile()});
    QCOMPARE(readableSocket.write(readablePayload), readablePayload.size());
    readableSocket.flush();
    QTRY_COMPARE(QDir::cleanPath(shell->m_tabs.first().part->url().toLocalFile()), QDir::cleanPath(readableUrl.toLocalFile()));
    QTRY_VERIFY_WITH_TIMEOUT(readableSocket.bytesAvailable() > 0, 1000);
    QCOMPARE(readableSocket.read(1), QByteArrayLiteral("A"));
}

void MainShellTest::testNewWindowActionAppearsInFileMenu()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QAction *action = shell->actionCollection()->action(QStringLiteral("file_open_new_window"));
    QVERIFY(action);
    bool inFileMenu = false;
    for (QAction *menuAction : shell->menuBar()->actions()) {
        if (menuAction->objectName() == QLatin1String("file") && menuAction->menu()) {
            inFileMenu = menuAction->menu()->actions().contains(action);
        }
    }
    QVERIFY(inFileMenu);
}

void MainShellTest::testMoveTabToNewWindowPreservesPart()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *source = findShell();
    QVERIFY(source);
    const QUrl first = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl second = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    source->openUrl(first);
    source->openUrl(second);
    QCOMPARE(source->m_tabs.size(), 2);
    KParts::ReadWritePart *movedPart = source->m_tabs.at(1).part;
    QVERIFY(source->moveTabToNewWindow(1));
    QCOMPARE(source->m_tabs.size(), 1);
    QCOMPARE(source->m_tabs.first().part->url(), first);
    const QList<Shell *> afterFirstMove = getShells();
    QCOMPARE(afterFirstMove.size(), 2);
    Shell *destination = afterFirstMove.at(0) == source ? afterFirstMove.at(1) : afterFirstMove.at(0);
    QCOMPARE(destination->m_tabs.first().part, movedPart);
    QCOMPARE(destination->m_tabs.first().part->url(), second);
    if (QScreen *screen = destination->screen(); screen && QGuiApplication::platformName() != QLatin1String("offscreen")) {
        const QRect available = screen->availableGeometry();
        QVERIFY(available.contains(destination->frameGeometry()));
        QVERIFY(destination->frameGeometry().width() >= available.width() * 9 / 10);
        QVERIFY(destination->frameGeometry().height() >= available.height() * 9 / 10);
    }
    QCOMPARE(Shell::savedOpenDocumentSessionWindowIds().size(), 2);

    KParts::ReadWritePart *lastPart = source->m_tabs.first().part;
    QVERIFY(source->moveTabToNewWindow(0));
    QCOMPARE(source->m_tabs.size(), 1);
    QVERIFY(source->m_tabs.first().part->url().isEmpty());
    const QList<Shell *> afterSecondMove = getShells();
    QCOMPARE(afterSecondMove.size(), 3);
    QVERIFY(std::any_of(afterSecondMove.cbegin(), afterSecondMove.cend(), [lastPart](Shell *shell) { return shell->m_tabs.first().part == lastPart; }));
}

void MainShellTest::testSelectAndCloseSeveralTabs()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")),
    };
    for (const QUrl &url : urls) {
        shell->openUrl(url);
    }
    QTabBar *bar = shell->m_tabWidget->tabBar();
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    QTest::mouseClick(bar, Qt::LeftButton, Qt::ShiftModifier, bar->tabRect(1).center());
    QCOMPARE(shell->selectedTabsFor(1), (QList<int> {0, 1}));
    shell->closeUrl();
    QCOMPARE(shell->m_tabs.size(), 1);
    QCOMPARE(shell->m_tabs.first().part->url(), urls.at(2));
}

void MainShellTest::testCloseSelectedTabsWithCloseButton()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")),
    };
    for (const QUrl &url : urls) {
        shell->openUrl(url);
    }
    QTabBar *bar = shell->m_tabWidget->tabBar();
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    QTest::mouseClick(bar, Qt::LeftButton, Qt::ShiftModifier, bar->tabRect(1).center());
    QPointer<QWidget> closeButton = bar->tabButton(1, QTabBar::RightSide);
    if (!closeButton) {
        closeButton = bar->tabButton(1, QTabBar::LeftSide);
    }
    QVERIFY(closeButton);
    QTest::mouseClick(closeButton, Qt::LeftButton);
    QCOMPARE(shell->m_tabs.size(), 1);
    QCOMPARE(shell->m_tabs.first().part->url(), urls.at(2));
}

void MainShellTest::testControlClickSelectsSeparateTabs()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")),
    };
    for (const QUrl &url : urls) {
        shell->openUrl(url);
    }
    QTabBar *bar = shell->m_tabWidget->tabBar();
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    QTest::mouseClick(bar, Qt::LeftButton, Qt::ControlModifier, bar->tabRect(2).center());
    QCOMPARE(shell->selectedTabsFor(2), (QList<int> {0, 2}));
    QVERIFY(shell->m_closeAction->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_W)));
    QVERIFY(shell->m_closeAction->isEnabled());
    shell->m_closeAction->trigger();
    QCOMPARE(shell->m_tabs.size(), 1);
    QCOMPARE(shell->m_tabs.first().part->url(), urls.at(1));
}

void MainShellTest::testMoveSelectedTabsBetweenWindows()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *source = findShell();
    QVERIFY(source);
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")),
    };
    for (const QUrl &url : urls) {
        source->openUrl(url);
    }
    QTabBar *bar = source->m_tabWidget->tabBar();
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    QTest::mouseClick(bar, Qt::LeftButton, Qt::ShiftModifier, bar->tabRect(1).center());
    QCOMPARE(source->selectedTabsFor(1), (QList<int> {0, 1}));
    KParts::ReadWritePart *firstPart = source->m_tabs.at(0).part;
    KParts::ReadWritePart *secondPart = source->m_tabs.at(1).part;
    QVERIFY(source->moveTabsToWindow(source->selectedTabsFor(1)));
    QCOMPARE(source->m_tabs.size(), 1);
    QCOMPARE(source->m_tabs.first().part->url(), urls.at(2));
    const QList<Shell *> windows = getShells();
    QCOMPARE(windows.size(), 2);
    Shell *destination = windows.at(0) == source ? windows.at(1) : windows.at(0);
    QCOMPARE(destination->m_tabs.size(), 2);
    QCOMPARE(destination->m_tabs.at(0).part, firstPart);
    QCOMPARE(destination->m_tabs.at(1).part, secondPart);
    QCOMPARE(destination->m_tabs.at(0).part->url(), urls.at(0));
    QCOMPARE(destination->m_tabs.at(1).part->url(), urls.at(1));
    QVERIFY(source->moveTabsToWindow({0}, destination));
    QCOMPARE(destination->m_tabs.size(), 3);
    QCOMPARE(destination->m_tabs.at(2).part->url(), urls.at(2));
    QVERIFY(source->m_tabs.first().part->url().isEmpty());
}

void MainShellTest::testMoveSelectedTabsViaMenu()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *source = findShell();
    QVERIFY(source);
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")),
        QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")),
    };
    for (const QUrl &url : urls) {
        source->openUrl(url);
    }
    QTabBar *bar = source->m_tabWidget->tabBar();
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    QTest::mouseClick(bar, Qt::LeftButton, Qt::ShiftModifier, bar->tabRect(1).center());
    QTimer::singleShot(0, source, []() {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(menu->actions().first()).center());
        }
    });
    QContextMenuEvent context(QContextMenuEvent::Mouse, bar->tabRect(1).center(), bar->mapToGlobal(bar->tabRect(1).center()));
    QApplication::sendEvent(bar, &context);
    const QList<Shell *> windows = getShells();
    QCOMPARE(windows.size(), 2);
    Shell *destination = windows.at(0) == source ? windows.at(1) : windows.at(0);
    QCOMPARE(source->m_tabs.size(), 1);
    QCOMPARE(destination->m_tabs.size(), 2);
    QCOMPARE(destination->m_tabs.at(0).part->url().toLocalFile(), urls.at(0).toLocalFile());
    QCOMPARE(destination->m_tabs.at(1).part->url().toLocalFile(), urls.at(1).toLocalFile());
}

void MainShellTest::testMoveToExistingWindowMenu()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *source = findShell();
    QVERIFY(source);
    source->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    Shell *target = new Shell(options, QString(), true);
    QVERIFY(target->isValid());
    target->show();
    target->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")));
    QTabBar *bar = source->m_tabWidget->tabBar();
    bool found = false;
    QTimer::singleShot(0, source, [&found]() {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            found = menu->actions().size() == 2 && menu->actions().at(1)->text().contains(QStringLiteral("file2.pd"));
            if (found) {
                QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(menu->actions().at(1)).center());
            } else {
                menu->close();
            }
        }
    });
    QContextMenuEvent context(QContextMenuEvent::Mouse, bar->tabRect(0).center(), bar->mapToGlobal(bar->tabRect(0).center()));
    QApplication::sendEvent(bar, &context);
    QVERIFY(found);
    QCOMPARE(target->m_tabs.size(), 2);
    QCOMPARE(target->m_tabs.at(1).part->url().toLocalFile(), QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")).toLocalFile());

    source->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")));
    target->setActiveTab(0);
    const QString longTitle = QStringLiteral("A very long document title that describes the whole research collection and keeps going well beyond any sensible tab width.pdf");
    target->m_tabWidget->setTabText(0, longTitle);
    bool capped = false;
    QTimer::singleShot(0, source, [&capped, &longTitle]() {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            const QAction *destination = menu->actions().value(1);
            capped = destination && menu->sizeHint().width() <= 450 && destination->text().contains(QStringLiteral("…")) && destination->toolTip().contains(QStringLiteral("file2.pdf"))
                && !destination->text().contains(longTitle);
            menu->close();
        }
    });
    QContextMenuEvent longContext(QContextMenuEvent::Mouse, bar->tabRect(0).center(), bar->mapToGlobal(bar->tabRect(0).center()));
    QApplication::sendEvent(bar, &longContext);
    QVERIFY(capped);
}

void MainShellTest::testOrdinaryOpenFocusesTabInOtherWindow()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *source = findShell();
    QVERIFY(source);
    const QUrl first = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl second = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    source->openUrl(first);
    source->openUrl(second);
    QVERIFY(source->moveTabToNewWindow(1));
    const QList<Shell *> windows = getShells();
    QCOMPARE(windows.size(), 2);
    Shell *detached = windows.at(0) == source ? windows.at(1) : windows.at(0);
    const KParts::ReadWritePart *const movedPart = detached->m_tabs.first().part;

    source->openUrl(second);
    QCOMPARE(source->m_tabs.size(), 1);
    QCOMPARE(source->m_tabs.first().part->url(), first);
    QCOMPARE(detached->m_tabs.size(), 1);
    QCOMPARE(detached->m_tabs.first().part, movedPart);
    QCOMPARE(detached->m_tabWidget->currentIndex(), 0);

    // Explicitly opening a document in an intentional new window remains possible.
    Shell *intentional = new Shell(QString(), QString(), true);
    QVERIFY(intentional->isValid());
    intentional->show();
    QVERIFY(intentional->openDocumentInTab(second, QString()));
    QCOMPARE(intentional->m_tabs.first().part->url(), second);
}

void MainShellTest::testRecentFilesStayConsistentAcrossWindows()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    second->show();
    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    first->openUrl(firstUrl);
    second->openUrl(secondUrl);
    QVERIFY(first->m_recent->urls().contains(firstUrl));
    QVERIFY(first->m_recent->urls().contains(secondUrl));
    QVERIFY(second->m_recent->urls().contains(firstUrl));
    QVERIFY(second->m_recent->urls().contains(secondUrl));

    first->forgetRecentItems({firstUrl});
    QVERIFY(!first->m_recent->urls().contains(firstUrl));
    QVERIFY(!second->m_recent->urls().contains(firstUrl));
    second->saveOpenDocumentSession();
    Shell *third = new Shell(options, QString(), true);
    QVERIFY(third->isValid());
    QVERIFY(!third->m_recent->urls().contains(firstUrl));
    QVERIFY(third->m_recent->urls().contains(secondUrl));
}

void MainShellTest::testOpenRecentFocusesDocumentInOtherWindow()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *first = findShell();
    QVERIFY(first);
    Shell *second = new Shell(options, QString(), true);
    QVERIFY(second->isValid());
    second->show();
    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    first->openUrl(firstUrl);
    second->openUrl(secondUrl);
    QWidget *notice = second->m_tabs.first().part->widget()->findChild<QWidget *>(QStringLiteral("pageViewMessage"));
    QVERIFY(notice);
    const bool oldShowOSD = Okular::Settings::showOSD();
    const auto restoreSetting = qScopeGuard([&]() { Okular::Settings::self()->setShowOSD(oldShowOSD); });
    Okular::Settings::self()->setShowOSD(true);
    notice->hide();
    first->openRecentDocuments({secondUrl});
    QCOMPARE(first->m_tabs.size(), 1);
    QCOMPARE(first->m_tabs.first().part->url(), firstUrl);
    QCOMPARE(second->m_tabs.size(), 1);
    QCOMPARE(second->m_tabs.first().part->url(), secondUrl);
    QVERIFY(notice->isVisible());
    QTRY_VERIFY(second->isActiveWindow());

    notice = first->m_tabs.first().part->widget()->findChild<QWidget *>(QStringLiteral("pageViewMessage"));
    QVERIFY(notice);
    notice->hide();
    first->openRecentDocuments({firstUrl});
    QVERIFY(notice->isVisible());
}

void MainShellTest::testDragTabOutsideWindowDetaches()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *source = findShell();
    QVERIFY(source);
    source->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    source->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")));
    auto *bar = source->m_tabWidget->tabBar();
    KParts::ReadWritePart *part = source->m_tabs.at(1).part;
    QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(1).center());
    const QPoint outside = source->frameGeometry().bottomRight() + QPoint(100, 100);
    QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, bar->mapFromGlobal(outside));
    QTRY_COMPARE(getShells().size(), 2);
    QCOMPARE(source->m_tabs.size(), 1);
    const QList<Shell *> windows = getShells();
    Shell *destination = windows.at(0) == source ? windows.at(1) : windows.at(0);
    QCOMPARE(destination->m_tabs.first().part, part);
    if (QScreen *screen = destination->screen(); screen && QGuiApplication::platformName() != QLatin1String("offscreen")) {
        const QRect available = screen->availableGeometry();
        QVERIFY(available.contains(destination->frameGeometry()));
        QVERIFY(destination->frameGeometry().width() >= available.width() * 9 / 10);
        QVERIFY(destination->frameGeometry().height() >= available.height() * 9 / 10);
    }
}

void MainShellTest::testDroppedFileOpensNewTab()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    Okular::Settings::self()->setShellOpenFileInTabs(false);

    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    shell->openUrl(firstUrl);
    KParts::ReadWritePart *part = shell->m_tabs.first().part;
    QVERIFY(QMetaObject::invokeMethod(part, "handleDroppedUrls", Q_ARG(QList<QUrl>, QList<QUrl> {secondUrl})));
    QCOMPARE(shell->m_tabs.size(), 2);
    QCOMPARE(QDir::cleanPath(shell->m_tabs.first().part->url().toLocalFile()), QDir::cleanPath(firstUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.last().part->url().toLocalFile()), QDir::cleanPath(secondUrl.toLocalFile()));
}
#endif

void MainShellTest::testOpenDocumentSessionRestoresTabs()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup session = config->group(QStringLiteral("Shell Open Documents Session"));
    session.deleteGroup();
    config->sync();

    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));

    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->openUrl(firstUrl);
    shell->openUrl(secondUrl);
    QCOMPARE(shell->m_tabs.size(), 2);
    shell->m_tabWidget->setCurrentIndex(0);
    shell->saveOpenDocumentSession();
    QCOMPARE(session.readPathEntry("Urls", QStringList()).size(), 2);

    delete shell;
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *restored = findShell();
    QVERIFY(restored);
    QCOMPARE(restored->m_tabs.size(), 2);
    QCOMPARE(QDir::cleanPath(restored->m_tabs.at(0).part->url().toLocalFile()), QDir::cleanPath(firstUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(restored->m_tabs.at(1).part->url().toLocalFile()), QDir::cleanPath(secondUrl.toLocalFile()));
    QCOMPARE(restored->m_tabWidget->currentIndex(), 0);

    delete restored;
    session.deleteGroup();
    config->sync();
}

void MainShellTest::testFileLaunchRestoresTabsBeforeOpeningFile()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup session = config->group(QStringLiteral("Shell Open Documents Session"));
    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    const QUrl newUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/formattest.pdf"));
    session.writePathEntry("Urls", QStringList {firstUrl.url(), secondUrl.url()});
    session.writeEntry("ActiveTab", 0);
    config->sync();

    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList {newUrl.toLocalFile()}, options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QCOMPARE(shell->m_tabs.size(), 3);
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(0).part->url().toLocalFile()), QDir::cleanPath(firstUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(1).part->url().toLocalFile()), QDir::cleanPath(secondUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(2).part->url().toLocalFile()), QDir::cleanPath(newUrl.toLocalFile()));
    QCOMPARE(shell->m_tabWidget->currentIndex(), 2);
}

void MainShellTest::testDisabledOpenDocumentSessionRestore()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup session = config->group(QStringLiteral("Shell Open Documents Session"));
    const QUrl savedUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    session.writePathEntry("Urls", QStringList {savedUrl.url()});
    config->sync();
    const QString partConfigPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QLatin1String("/okularpartrc");
    KSharedConfigPtr partConfig = KSharedConfig::openConfig(partConfigPath);
    partConfig->group(QStringLiteral("General")).writeEntry("ShellRestoreOpenDocuments", false);
    partConfig->sync();

    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QCOMPARE(shell->m_tabs.size(), 1);
    QVERIFY(shell->m_tabs.at(0).part->url().isEmpty());
}

void MainShellTest::testOpenDocumentSessionIgnoresNewFileTabPreference()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup session = config->group(QStringLiteral("Shell Open Documents Session"));
    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    session.writePathEntry("Urls", QStringList {firstUrl.url(), secondUrl.url()});
    config->sync();
    Okular::Settings::self()->setShellOpenFileInTabs(false);

    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QCOMPARE(shell->m_tabs.size(), 2);
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(0).part->url().toLocalFile()), QDir::cleanPath(firstUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(1).part->url().toLocalFile()), QDir::cleanPath(secondUrl.toLocalFile()));
    QVERIFY(findShell(shell) == nullptr);

    // A file argument on this launch must follow the restored set even if
    // ordinary new-file tabbing is disabled on this platform.
    delete shell;
    session.writePathEntry("Urls", QStringList {firstUrl.url(), secondUrl.url()});
    config->sync();
    QCOMPARE(Okular::main(QStringList {QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/formattest.pdf")).toLocalFile()}, options), Okular::Success);
    shell = findShell();
    QVERIFY(shell);
    QCOMPARE(shell->m_tabs.size(), 3);
    QVERIFY(findShell(shell) == nullptr);
}

void MainShellTest::testOpenDocumentSessionSkipsMissingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl missingUrl = QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing.pdf")));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup session = config->group(QStringLiteral("Shell Open Documents Session"));
    session.writePathEntry("Urls", QStringList {firstUrl.url(), missingUrl.url(), secondUrl.url()});
    session.writeEntry("ActiveTab", 2);
    config->sync();

    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QCOMPARE(shell->m_tabs.size(), 2);
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(0).part->url().toLocalFile()), QDir::cleanPath(firstUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(1).part->url().toLocalFile()), QDir::cleanPath(secondUrl.toLocalFile()));
    QCOMPARE(shell->m_tabWidget->currentIndex(), 1);
}

#if defined(Q_OS_WIN)
void MainShellTest::testOpenDocumentSessionDeduplicatesSavedUrls()
{
    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup session = config->group(QStringLiteral("Shell Open Documents Session"));
    session.writePathEntry("Urls", QStringList {firstUrl.url(), firstUrl.url(), secondUrl.url()});
    session.writeEntry("ActiveTab", 1);
    config->sync();
    Okular::Settings::self()->setSwitchToTabIfOpen(false);

    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QCOMPARE(shell->m_tabs.size(), 2);
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(0).part->url().toLocalFile()), QDir::cleanPath(firstUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(1).part->url().toLocalFile()), QDir::cleanPath(secondUrl.toLocalFile()));
    QCOMPARE(shell->m_tabWidget->currentIndex(), 0);
}
#endif

void MainShellTest::testOpenAllRecentDocumentsAsTabs()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    const QUrl missingUrl = QUrl::fromLocalFile(QDir::tempPath() + QStringLiteral("/okular-missing-recent-document.pdf"));
    QVERIFY(!QFile::exists(missingUrl.toLocalFile()));
    shell->m_recent->clear();
    shell->addRecentUrl(firstUrl);
    shell->addRecentUrl(secondUrl);
    shell->addRecentUrl(missingUrl);
    shell->refreshRecentsOnWelcomeScreen();

    QToolButton *openAllButton = shell->m_welcomeScreen->findChild<QToolButton *>(QStringLiteral("openAllRecentsButton"));
    QVERIFY(openAllButton);
    QVERIFY(openAllButton->isEnabled());
    shell->openUrl(firstUrl);
    QCOMPARE(shell->m_tabs.size(), 1);

    openAllButton->click();
    QCOMPARE(shell->m_tabs.size(), 2);
    QCOMPARE(shell->m_tabs.at(0).part->url(), firstUrl);
    QCOMPARE(shell->m_tabs.at(1).part->url(), secondUrl);
    QCOMPARE(shell->m_tabWidget->currentIndex(), 1);

    shell->m_tabWidget->setCurrentIndex(0);
    openAllButton->click();
    QCOMPARE(shell->m_tabs.size(), 2);
    QCOMPARE(shell->m_tabWidget->currentIndex(), 1);
    shell->m_recent->clear();
    shell->refreshRecentsOnWelcomeScreen();
    QVERIFY(!openAllButton->isEnabled());
}

void MainShellTest::testOpenAllIgnoresRecentSelection()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    const QUrl thirdUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/formattest.pdf"));
    shell->m_recent->clear();
    shell->addRecentUrl(firstUrl);
    shell->addRecentUrl(secondUrl);
    shell->addRecentUrl(thirdUrl);
    shell->refreshRecentsOnWelcomeScreen();

    QListView *view = shell->m_welcomeScreen->findChild<QListView *>(QStringLiteral("recentsListView"));
    QToolButton *openAll = shell->m_welcomeScreen->findChild<QToolButton *>(QStringLiteral("openAllRecentsButton"));
    QVERIFY(view);
    QVERIFY(openAll);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, view->visualRect(view->model()->index(2, 0)).center());
    QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
    QTest::mouseClick(openAll, Qt::LeftButton);

    QCOMPARE(shell->m_tabs.size(), 3);
    QCOMPARE(shell->m_tabs.at(0).part->url(), firstUrl);
    QCOMPARE(shell->m_tabs.at(1).part->url(), secondUrl);
    QCOMPARE(shell->m_tabs.at(2).part->url(), thirdUrl);
}

void MainShellTest::testRecentFilesExpireAfterThreeDays()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    const QUrl olderUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl recentUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    shell->m_recent->clear();
    shell->addRecentUrl(olderUrl);
    shell->addRecentUrl(recentUrl);
    const QByteArray oldKey = QCryptographicHash::hash(olderUrl.toString(QUrl::FullyEncoded).toUtf8(), QCryptographicHash::Sha256).toHex();
    KSharedConfigPtr recentConfig = testShellSessionConfig();
    recentConfig->group(QStringLiteral("Recent File Open Times")).writeEntry(oldKey.constData(), QDateTime::currentSecsSinceEpoch() - 3 * 24 * 60 * 60 - 1);
    shell->refreshRecentsOnWelcomeScreen();

    QCOMPARE(shell->m_recent->urls(), (QList<QUrl> {recentUrl}));
    QListView *view = shell->m_welcomeScreen->findChild<QListView *>(QStringLiteral("recentsListView"));
    QVERIFY(view);
    QCOMPARE(view->model()->rowCount(), 1);
}

void MainShellTest::testOpenAllRecentDocumentsFromScrolledList()
{
    QTemporaryDir directory(QDir::currentPath() + QStringLiteral("/okular-recent-pages-XXXXXX"));
    QVERIFY(directory.isValid());
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->m_recent->clear();
    QList<QUrl> urls;
    for (int i = 0; i < 11; ++i) {
        const QString path = directory.filePath(QStringLiteral("recent-%1.pdf").arg(i));
        QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), path));
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        const QUrl url = QUrl::fromLocalFile(path);
        urls.append(url);
        shell->addRecentUrl(url);
    }
    shell->refreshRecentsOnWelcomeScreen();
    QListView *view = shell->m_welcomeScreen->findChild<QListView *>(QStringLiteral("recentsListView"));
    QToolButton *openAll = shell->m_welcomeScreen->findChild<QToolButton *>(QStringLiteral("openAllRecentsButton"));
    QVERIFY(view);
    QVERIFY(openAll);
    QCOMPARE(view->model()->rowCount(), 11);
    view->scrollTo(view->model()->index(10, 0));
    QTest::mouseClick(openAll, Qt::LeftButton);
    QCOMPARE(shell->m_tabs.size(), 11);
    QCOMPARE(shell->m_tabs.first().part->url(), urls.first());
    QCOMPARE(shell->m_tabs.last().part->url(), urls.last());
    delete shell;
}

void MainShellTest::testRecentFilesLimitedToFifty()
{
    // KRecentFilesAction deliberately ignores the system temporary directory.
    QTemporaryDir directory(QDir::currentPath() + QStringLiteral("/okular-recent-limit-XXXXXX"));
    QVERIFY(directory.isValid());
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->m_recent->clear();
    QCOMPARE(shell->m_recent->maxItems(), 50);
    for (int i = 0; i < 51; ++i) {
        const QString path = directory.filePath(QStringLiteral("recent-%1.pdf").arg(i));
        QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), path));
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        shell->addRecentUrl(QUrl::fromLocalFile(path));
    }
    QCOMPARE(shell->m_recent->urls().size(), 50);
    shell->refreshRecentsOnWelcomeScreen();
    QCOMPARE(shell->m_recent->urls().size(), 50);
    QListView *view = shell->m_welcomeScreen->findChild<QListView *>(QStringLiteral("recentsListView"));
    QVERIFY(view);
    QCOMPARE(view->model()->rowCount(), 50);
    view->setFixedHeight(160);
    QTRY_VERIFY(view->verticalScrollBar()->maximum() > 0);
    view->scrollTo(view->model()->index(49, 0));
    QVERIFY(view->visualRect(view->model()->index(49, 0)).intersects(view->viewport()->rect()));
    delete shell;
}

void MainShellTest::testSelectRecentDocumentsAcrossScrollRange()
{
    QTemporaryDir directory(QDir::currentPath() + QStringLiteral("/okular-recent-scroll-XXXXXX"));
    QVERIFY(directory.isValid());
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->m_recent->clear();
    QList<QUrl> urls;
    for (int i = 0; i < 25; ++i) {
        const QString path = directory.filePath(QStringLiteral("recent-%1.pdf").arg(i));
        QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), path));
        const QUrl url = QUrl::fromLocalFile(path);
        urls.append(url);
        shell->addRecentUrl(url);
    }
    shell->refreshRecentsOnWelcomeScreen();
    QListView *view = shell->m_welcomeScreen->findChild<QListView *>(QStringLiteral("recentsListView"));
    QToolButton *openSelected = shell->m_welcomeScreen->findChild<QToolButton *>(QStringLiteral("openSelectedRecentsButton"));
    QVERIFY(view);
    QVERIFY(openSelected);
    QCOMPARE(view->model()->rowCount(), 25);
    view->setFixedHeight(160);
    QTRY_VERIFY(view->verticalScrollBar()->maximum() > 0);
    view->scrollTo(view->model()->index(0, 0));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    view->scrollTo(view->model()->index(24, 0));
    QVERIFY(view->verticalScrollBar()->value() > 0);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, view->visualRect(view->model()->index(24, 0)).center());
    QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
    QTest::mouseClick(openSelected, Qt::LeftButton);
    QCOMPARE(shell->m_tabs.size(), 2);
    QCOMPARE(shell->m_tabs.first().part->url(), urls.first());
    QCOMPARE(shell->m_tabs.last().part->url(), urls.last());
    delete shell;
}

void MainShellTest::testSelectRecentDocumentsWithCtrlClick()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    const QUrl oldestUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl middleUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    const QUrl newestUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/formattest.pdf"));
    shell->m_recent->clear();
    shell->addRecentUrl(oldestUrl);
    shell->addRecentUrl(middleUrl);
    shell->addRecentUrl(newestUrl);
    shell->refreshRecentsOnWelcomeScreen();

    QListView *view = shell->m_welcomeScreen->findChild<QListView *>(QStringLiteral("recentsListView"));
    QToolButton *openSelected = shell->m_welcomeScreen->findChild<QToolButton *>(QStringLiteral("openSelectedRecentsButton"));
    QVERIFY(view);
    QVERIFY(openSelected);
    QCOMPARE(view->selectionMode(), QAbstractItemView::ExtendedSelection);
    QCOMPARE(view->model()->rowCount(), 3);
    QVERIFY(!openSelected->isEnabled());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, view->visualRect(view->model()->index(2, 0)).center());
    QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
    QVERIFY(openSelected->isEnabled());
    shell->refreshRecentsOnWelcomeScreen();
    QVERIFY(view->selectionModel()->selectedRows().isEmpty());
    QVERIFY(!openSelected->isEnabled());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, view->visualRect(view->model()->index(2, 0)).center());
    openSelected->click();

    QCOMPARE(shell->m_tabs.size(), 2);
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(0).part->url().toLocalFile()), QDir::cleanPath(oldestUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(1).part->url().toLocalFile()), QDir::cleanPath(newestUrl.toLocalFile()));
    QCOMPARE(shell->m_tabWidget->currentIndex(), 1);
}

void MainShellTest::testSelectRecentDocumentsWithShiftClick()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    const QUrl thirdUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/formattest.pdf"));
    shell->m_recent->clear();
    shell->addRecentUrl(firstUrl);
    shell->addRecentUrl(secondUrl);
    shell->addRecentUrl(thirdUrl);
    shell->refreshRecentsOnWelcomeScreen();

    QListView *view = shell->m_welcomeScreen->findChild<QListView *>(QStringLiteral("recentsListView"));
    QToolButton *openSelected = shell->m_welcomeScreen->findChild<QToolButton *>(QStringLiteral("openSelectedRecentsButton"));
    QVERIFY(view);
    QVERIFY(openSelected);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ShiftModifier, view->visualRect(view->model()->index(2, 0)).center());
    QCOMPARE(view->selectionModel()->selectedRows().size(), 3);
    openSelected->click();

    QCOMPARE(shell->m_tabs.size(), 3);
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(0).part->url().toLocalFile()), QDir::cleanPath(firstUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(1).part->url().toLocalFile()), QDir::cleanPath(secondUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(2).part->url().toLocalFile()), QDir::cleanPath(thirdUrl.toLocalFile()));
    QCOMPARE(shell->m_tabWidget->currentIndex(), 2);
}

void MainShellTest::testRecentContextMenuSelectedActions()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    const QUrl firstUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl secondUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    const QUrl thirdUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/formattest.pdf"));
    shell->m_recent->clear();
    shell->addRecentUrl(firstUrl);
    shell->addRecentUrl(secondUrl);
    shell->addRecentUrl(thirdUrl);
    shell->refreshRecentsOnWelcomeScreen();
    QListView *view = shell->m_welcomeScreen->findChild<QListView *>(QStringLiteral("recentsListView"));
    QVERIFY(view);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, view->visualRect(view->model()->index(2, 0)).center());
    QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
    QCOMPARE(shell->m_welcomeScreen->recentUrlsForContextMenu(view->model()->index(0, 0)), (QList<QUrl> {thirdUrl, firstUrl}));
    QCOMPARE(shell->m_welcomeScreen->recentUrlsForContextMenu(view->model()->index(1, 0)), (QList<QUrl> {secondUrl}));
    QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, view->visualRect(view->model()->index(2, 0)).center());

    bool sawContextMenu = false;
    bool sawPluralActions = false;
    QTimer::singleShot(0, this, [&]() {
        QMenu *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        sawContextMenu = true;
        const QList<QAction *> actions = menu->actions();
        if (actions.size() == 5) {
            sawPluralActions = actions.at(0)->text() == i18n("&Open Selected") && actions.at(2)->text() == i18np("&Copy Path", "&Copy Paths", 2)
                && actions.at(3)->text() == i18np("&Open Containing Folder", "&Open Containing Folders", 2) && actions.at(3)->isEnabled()
                && actions.at(4)->text() == i18ncp("recent items context menu", "&Forget This Item", "&Forget These Items", 2);
            actions.at(4)->trigger();
        }
        menu->close();
    });
    QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    QVERIFY(sawContextMenu);
    QVERIFY(sawPluralActions);
    QCOMPARE(shell->m_recent->urls(), (QList<QUrl> {secondUrl}));
}

void MainShellTest::testRecentContextMenuOpensSelected()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    const QUrl olderUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl middleUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    const QUrl newerUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/formattest.pdf"));
    shell->m_recent->clear();
    shell->addRecentUrl(olderUrl);
    shell->addRecentUrl(middleUrl);
    shell->addRecentUrl(newerUrl);
    shell->refreshRecentsOnWelcomeScreen();
    QListView *view = shell->m_welcomeScreen->findChild<QListView *>(QStringLiteral("recentsListView"));
    QVERIFY(view);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, view->visualRect(view->model()->index(2, 0)).center());

    bool openedFromMenu = false;
    QTimer::singleShot(0, this, [&]() {
        QMenu *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (menu && !menu->actions().isEmpty()) {
            openedFromMenu = true;
            menu->actions().first()->trigger();
            menu->close();
        }
    });
    QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    QVERIFY(openedFromMenu);
    QCOMPARE(shell->m_tabs.size(), 2);
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(0).part->url().toLocalFile()), QDir::cleanPath(olderUrl.toLocalFile()));
    QCOMPARE(QDir::cleanPath(shell->m_tabs.at(1).part->url().toLocalFile()), QDir::cleanPath(newerUrl.toLocalFile()));
    QCOMPARE(shell->m_tabWidget->currentIndex(), 1);
}

void MainShellTest::testRecentContextMenuCopiesSelectedPaths()
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        QSKIP("Clipboard test runs only with an isolated offscreen Qt platform");
    }
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    const QUrl olderUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QUrl newerUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));
    shell->m_recent->clear();
    shell->addRecentUrl(olderUrl);
    shell->addRecentUrl(newerUrl);
    shell->refreshRecentsOnWelcomeScreen();
    QListView *view = shell->m_welcomeScreen->findChild<QListView *>(QStringLiteral("recentsListView"));
    QVERIFY(view);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, view->visualRect(view->model()->index(1, 0)).center());
    const QString oldClipboardText = QGuiApplication::clipboard()->text();
    bool copied = false;
    QTimer::singleShot(0, this, [&]() {
        QMenu *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (menu && menu->actions().size() == 5) {
            copied = true;
            menu->actions().at(2)->trigger();
            menu->close();
        }
    });
    QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, view->visualRect(view->model()->index(0, 0)).center());
    const QString copiedText = QGuiApplication::clipboard()->text();
    QGuiApplication::clipboard()->setText(oldClipboardText);
    QVERIFY(copied);
    QCOMPARE(copiedText, newerUrl.toLocalFile() + QLatin1Char('\n') + olderUrl.toLocalFile());
}

void MainShellTest::testTabControlsRemainAvailable()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    Okular::Settings::self()->setShellOpenFileInTabs(true);
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);

    shell->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    QCoreApplication::processEvents();
    QCOMPARE(shell->m_tabWidget->count(), 1);
    QVERIFY(shell->m_tabWidget->tabBar()->isVisible());
    QVERIFY(shell->m_openTabButton->isVisible());

    shell->resize(640, 520);
    shell->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf")));
    shell->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/formattest.pdf")));
    QCoreApplication::processEvents();
    QCOMPARE(shell->m_tabWidget->count(), 3);
    QVERIFY(shell->m_openTabButton->isVisible());
    const QRect buttonRect(shell->m_openTabButton->mapTo(shell, QPoint(0, 0)), shell->m_openTabButton->size());
    QVERIFY(shell->rect().contains(buttonRect));
}

void MainShellTest::testOverflowArrowsSelectEveryTab()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QTest::qWait(200); // Let the Windows startup geometry timers finish.
    shell->resize(500, 520);

    const QString sourcePath = QStringLiteral(KDESRCDIR "data/file1.pdf");
    for (int i = 0; i < 9; ++i) {
        const QString path = directory.filePath(QStringLiteral("tab-%1.pdf").arg(i));
        QVERIFY(QFile::copy(sourcePath, path));
        shell->openUrl(QUrl::fromLocalFile(path));
    }
    shell->resize(500, 520);
    QCoreApplication::processEvents();
    shell->updateOpenTabButtonGeometry();
    QCOMPARE(shell->m_tabWidget->currentIndex(), 8);
    QTabBar *tabBar = shell->m_tabWidget->tabBar();
    QList<QToolButton *> scrollButtons = tabBar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    scrollButtons.removeAll(shell->m_centerActiveTabButton);
    scrollButtons.erase(std::remove_if(scrollButtons.begin(), scrollButtons.end(), [](QToolButton *button) { return !button->isVisible(); }), scrollButtons.end());
    std::sort(scrollButtons.begin(), scrollButtons.end(), [](QToolButton *a, QToolButton *b) { return a->x() < b->x(); });
    QVERIFY(scrollButtons.size() >= 2);
    QToolButton *const previousTabButton = scrollButtons.at(scrollButtons.size() - 2);
    QToolButton *const nextTabButton = scrollButtons.last();
    const QRect previousRect = previousTabButton->geometry();
    const QRect centerRect = shell->m_centerActiveTabButton->geometry();
    const QRect nextRect = nextTabButton->geometry();
    QCOMPARE(previousRect.width(), 20);
    QCOMPARE(centerRect.width(), 20);
    QCOMPARE(nextRect.width(), 20);
    QCOMPARE(previousRect.right() + 1, centerRect.left());
    QCOMPARE(centerRect.right() + 1, nextRect.left());
    QCOMPARE(previousRect.top(), centerRect.top());
    QCOMPARE(centerRect.top(), nextRect.top());
    QCOMPARE(previousRect.height(), centerRect.height());
    QCOMPARE(centerRect.height(), nextRect.height());
    QCOMPARE(tabBar->style()->pixelMetric(QStyle::PM_TabBarScrollButtonWidth, nullptr, tabBar), 30);
    QStyleOption styleOption;
    styleOption.initFrom(tabBar);
    const QRect reservedLeft = tabBar->style()->subElementRect(QStyle::SE_TabBarScrollLeftButton, &styleOption, tabBar);
    const QRect reservedRight = tabBar->style()->subElementRect(QStyle::SE_TabBarScrollRightButton, &styleOption, tabBar);
    QCOMPARE(previousRect.left(), reservedLeft.left());
    QCOMPARE(nextRect.right(), reservedRight.right());
    QCOMPARE(reservedLeft.right() + 1, reservedRight.left());
    QVERIFY(previousRect.left() > tabBar->width() / 2);
    const QRect nextRectInShell(nextTabButton->mapTo(shell, QPoint(0, 0)), nextTabButton->size());
    const QRect openRectInShell(shell->m_openTabButton->mapTo(shell, QPoint(0, 0)), shell->m_openTabButton->size());
    QCOMPARE(openRectInShell.left() - nextRectInShell.right() - 1, 3);

    for (int expected = 7; expected >= 0; --expected) {
        QVERIFY(previousTabButton->isEnabled());
        QTest::mouseClick(previousTabButton, Qt::LeftButton);
        QCOMPARE(shell->m_tabWidget->currentIndex(), expected);
    }
    QCoreApplication::processEvents();
    QVERIFY(!previousTabButton->isEnabled());
    QVERIFY(nextTabButton->isEnabled());
    for (int expected = 1; expected < 9; ++expected) {
        QVERIFY(nextTabButton->isEnabled());
        QTest::mouseClick(nextTabButton, Qt::LeftButton);
        QCOMPARE(shell->m_tabWidget->currentIndex(), expected);
    }
    QVERIFY(!nextTabButton->isEnabled());
    delete shell;
}

void MainShellTest::testRtlOverflowControlsStayInsideTabBar()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QTest::qWait(200); // Wait for the Windows startup geometry timers.
    shell->resize(500, 520);

    const QString sourcePath = QStringLiteral(KDESRCDIR "data/file1.pdf");
    for (int i = 0; i < 9; ++i) {
        const QString path = directory.filePath(QStringLiteral("rtl-tab-%1.pdf").arg(i));
        QVERIFY(QFile::copy(sourcePath, path));
        shell->openUrl(QUrl::fromLocalFile(path));
    }
    shell->resize(500, 520);
    QTabBar *tabBar = shell->m_tabWidget->tabBar();
    tabBar->setLayoutDirection(Qt::RightToLeft);
    shell->setActiveTab(4);
    QCoreApplication::processEvents();
    shell->updateOpenTabButtonGeometry();

    QList<QToolButton *> buttons = tabBar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    buttons.removeAll(shell->m_centerActiveTabButton);
    buttons.erase(std::remove_if(buttons.begin(), buttons.end(), [](QToolButton *button) { return !button->isVisible(); }), buttons.end());
    std::sort(buttons.begin(), buttons.end(), [](QToolButton *a, QToolButton *b) { return a->x() < b->x(); });
    QVERIFY(buttons.size() >= 2);
    const QRect firstRect = buttons.at(buttons.size() - 2)->geometry();
    const QRect centerRect = shell->m_centerActiveTabButton->geometry();
    const QRect lastRect = buttons.last()->geometry();
    QCOMPARE(firstRect.left(), 0);
    QCOMPARE(firstRect.width(), 20);
    QCOMPARE(firstRect.right() + 1, centerRect.left());
    QCOMPARE(centerRect.right() + 1, lastRect.left());
    QCOMPARE(lastRect.right(), 59);
    QCOMPARE(firstRect.top(), centerRect.top());
    QCOMPARE(centerRect.top(), lastRect.top());
    QCOMPARE(firstRect.height(), centerRect.height());
    QCOMPARE(centerRect.height(), lastRect.height());

    buttons.at(buttons.size() - 2)->click();
    QCOMPARE(shell->m_tabWidget->currentIndex(), 3);
    buttons.last()->click();
    QCOMPARE(shell->m_tabWidget->currentIndex(), 4);
    delete shell;
}

void MainShellTest::testCenterButtonFramesActiveTab()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QTest::qWait(200); // Wait for the Windows startup geometry timers.
    shell->resize(500, 520);
    const QString sourcePath = QStringLiteral(KDESRCDIR "data/file1.pdf");
    for (int i = 0; i < 9; ++i) {
        const QString path = directory.filePath(QStringLiteral("tab-%1.pdf").arg(i));
        QVERIFY(QFile::copy(sourcePath, path));
        shell->openUrl(QUrl::fromLocalFile(path));
    }
    shell->resize(500, 520);
    shell->setActiveTab(4);
    QCoreApplication::processEvents();
    shell->updateOpenTabButtonGeometry();
    QTabBar *tabBar = shell->m_tabWidget->tabBar();
    QList<QToolButton *> nativeButtons = tabBar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    nativeButtons.removeAll(shell->m_centerActiveTabButton);
    nativeButtons.erase(std::remove_if(nativeButtons.begin(), nativeButtons.end(), [](QToolButton *button) { return !button->isVisible(); }), nativeButtons.end());
    std::sort(nativeButtons.begin(), nativeButtons.end(), [](QToolButton *a, QToolButton *b) { return a->x() < b->x(); });
    QVERIFY(nativeButtons.size() >= 2);
    QToolButton *const rightScrollButton = nativeButtons.last();
    shell->m_centeringActiveTab = true;
    for (int i = 0; i < 9 && rightScrollButton->isEnabled(); ++i) {
        rightScrollButton->click();
        QCoreApplication::processEvents();
    }
    shell->m_centeringActiveTab = false;
    const int visibleCenter = (tabBar->rect().left() + nativeButtons.at(nativeButtons.size() - 2)->x() - 1) / 2;
    const int before = std::abs(tabBar->tabRect(4).center().x() - visibleCenter);
    shell->m_centerActiveTabButton->click();
    const int after = std::abs(tabBar->tabRect(4).center().x() - visibleCenter);
    QCOMPARE(shell->m_tabWidget->currentIndex(), 4);
    QVERIFY(after < before);
    delete shell;
}

void MainShellTest::testFileLaunchShowsDocumentViewFirst()
{
    class ShowProbe : public QObject
    {
    public:
        bool sawShell = false;
        bool showedDocumentView = false;

        bool eventFilter(QObject *object, QEvent *event) override
        {
            if (event->type() == QEvent::Show) {
                if (auto *shell = qobject_cast<Shell *>(object)) {
                    sawShell = true;
                    auto *stack = qobject_cast<QStackedWidget *>(shell->centralWidget());
                    showedDocumentView = stack && stack->currentWidget() != shell->findChild<WelcomeScreen *>();
                }
            }
            return false;
        }
    } probe;
    QApplication::instance()->installEventFilter(&probe);
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    const QString path = QStringLiteral(KDESRCDIR "data/file1.pdf");
    const Okular::Status status = Okular::main(QStringList {path}, options);
    QApplication::instance()->removeEventFilter(&probe);
    QCOMPARE(status, Okular::Success);
    QVERIFY(probe.sawShell);
    QVERIFY(probe.showedDocumentView);
}

void MainShellTest::testWelcomeHiddenBeforeDocumentLoad()
{
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    QCOMPARE(shell->m_centralStackedWidget->currentWidget(), static_cast<QWidget *>(shell->m_welcomeScreen));
    shell->openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    QCOMPARE(shell->m_centralStackedWidget->currentWidget(), static_cast<QWidget *>(shell->m_tabWidget));
    QVERIFY(!shell->m_welcomeScreen->isVisible());
}

void MainShellTest::testShell_data()
{
    QTest::addColumn<QStringList>("paths");
    QTest::addColumn<QString>("serializedOptions");
    QTest::addColumn<bool>("useTabs");
    QTest::addColumn<QString>("externalProcessPath");
    QTest::addColumn<uint>("expectedPage");
    QTest::addColumn<bool>("expectPresentation");
    QTest::addColumn<bool>("expectPrintDialog");
    QTest::addColumn<bool>("unique");
    QTest::addColumn<uint>("externalProcessExpectedPage");
    QTest::addColumn<bool>("externalProcessExpectPresentation");
    QTest::addColumn<bool>("externalProcessExpectPrintDialog");
    QTest::addColumn<QString>("externalProcessExpectFind");

    const QStringList contentsEpub = QStringList(QStringLiteral(KDESRCDIR "data/contents.epub"));
    const QStringList file1 = QStringList(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    QStringList file1AndToc;
    file1AndToc << QStringLiteral(KDESRCDIR "data/file1.pdf");
    file1AndToc << QStringLiteral(KDESRCDIR "data/tocreload.pdf");
    const QString tocReload = QStringLiteral(KDESRCDIR "data/tocreload.pdf");

    const QString optionsPage2 = ShellUtils::serializeOptions(false, false, false, false, false, QStringLiteral("2"), QString(), QString());
    const QString optionsPage2Presentation = ShellUtils::serializeOptions(true, false, false, false, false, QStringLiteral("2"), QString(), QString());
    const QString optionsPrint = ShellUtils::serializeOptions(false, true, false, false, false, QString(), QString(), QString());
    const QString optionsUnique = ShellUtils::serializeOptions(false, false, false, true, false, QString(), QString(), QString());
    const QString optionsFind = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QStringLiteral("si:next-testing parameters!"), QString());

    QTest::newRow("just show shell") << QStringList() << QString() << false << QString() << 0u << false << false << false << 0u << false << false << QString();
    QTest::newRow("open file") << file1 << QString() << false << QString() << 0u << false << false << false << 0u << false << false << QString();
    QTest::newRow("two files no tabs") << file1AndToc << QString() << false << QString() << 0u << false << false << false << 0u << false << false << QString();
    QTest::newRow("two files with tabs") << file1AndToc << QString() << true << QString() << 0u << false << false << false << 0u << false << false << QString();
    QTest::newRow("two files sequence no tabs") << file1 << QString() << false << tocReload << 0u << false << false << false << 0u << false << false << QString();
#if HAVE_DBUS
    QTest::newRow("two files sequence with tabs") << file1 << QString() << true << tocReload << 0u << false << false << false << 0u << false << false << QString();
#endif // HAVE_DBUS
    QTest::newRow("open file page number") << contentsEpub << optionsPage2 << false << QString() << 1u << false << false << false << 0u << false << false << QString();
    QTest::newRow("open file page number and presentation") << contentsEpub << optionsPage2Presentation << false << QString() << 1u << true << false << false << 0u << false << false << QString();
    QTest::newRow("open file find") << file1 << optionsFind << false << QString() << 0u << false << false << false << 0u << false << false << QStringLiteral("si:next-testing parameters!");
    QTest::newRow("open file print") << file1 << optionsPrint << false << QString() << 0u << false << true << false << 0u << false << false << QString();
#if HAVE_DBUS
    QTest::newRow("open two files unique") << file1 << optionsUnique << false << tocReload << 0u << false << false << true << 0u << false << false << QString();
    QTest::newRow("open two files unique tabs") << file1 << optionsUnique << true << tocReload << 0u << false << false << true << 0u << false << false << QString();
    QTest::newRow("page number attach tabs") << file1 << QString() << true << contentsEpub[0] << 0u << false << false << false << 2u << false << false << QString();
    QTest::newRow("presentation attach tabs") << file1 << QString() << true << contentsEpub[0] << 0u << false << false << false << 2u << true << false << QString();
    QTest::newRow("print attach tabs") << file1 << QString() << true << contentsEpub[0] << 0u << false << true << false << 2u << false << true << QString();
    QTest::newRow("page number attach unique") << file1 << optionsUnique << false << contentsEpub[0] << 0u << false << false << true << 3u << false << false << QString();
    QTest::newRow("presentation attach unique") << file1 << optionsUnique << false << contentsEpub[0] << 0u << false << false << true << 2u << true << false << QString();
    QTest::newRow("print attach unique") << file1 << optionsUnique << false << contentsEpub[0] << 0u << false << false << true << 2u << false << true << QString();
    QTest::newRow("page number attach unique tabs") << file1 << optionsUnique << true << contentsEpub[0] << 0u << false << false << true << 3u << false << false << QString();
    QTest::newRow("presentation attach unique tabs") << file1 << optionsUnique << true << contentsEpub[0] << 0u << false << false << true << 2u << true << false << QString();
    QTest::newRow("print attach unique tabs") << file1 << optionsUnique << true << contentsEpub[0] << 0u << false << false << true << 2u << false << true << QString();
#endif // HAVE_DBUS
}

void MainShellTest::testShell()
{
    QFETCH(QStringList, paths);
    QFETCH(QString, serializedOptions);
    QFETCH(bool, useTabs);
    QFETCH(QString, externalProcessPath);
    QFETCH(uint, expectedPage);
    QFETCH(bool, expectPresentation);
    QFETCH(bool, expectPrintDialog);
    QFETCH(bool, unique);
    QFETCH(uint, externalProcessExpectedPage);
    QFETCH(bool, externalProcessExpectPresentation);
    QFETCH(bool, externalProcessExpectPrintDialog);
    QFETCH(QString, externalProcessExpectFind);

    QScopedPointer<ClosePrintDialogHelper> helper;

    Okular::Settings::self()->setShellOpenFileInTabs(useTabs);
#if defined(Q_OS_WIN)
    const bool expectTabs = true;
#else
    const bool expectTabs = useTabs;
#endif

    if (expectPrintDialog || externalProcessExpectPrintDialog) {
        const int expectedTab = externalProcessExpectPrintDialog && !unique ? 1 : 0;
        helper.reset(new ClosePrintDialogHelper(expectedTab));
        QTimer::singleShot(0, helper.data(), &ClosePrintDialogHelper::closePrintDialog);
    }

    Okular::Status status = Okular::main(paths, serializedOptions);
    QCOMPARE(status, Okular::Success);
    Shell *s = findShell();
    QVERIFY(s);

    if (paths.count() == 1) {
        QCOMPARE(s->m_tabs.count(), 1);
        Okular::Part *part = s->findChild<Okular::Part *>();
        QVERIFY(part);
        QCOMPARE(part->url(), QUrl::fromLocalFile(paths[0]));
        QCOMPARE(partDocument(part)->currentPage(), expectedPage);
        // Testing if the bar is shown or hidden as expected
        QCOMPARE(findWidget(part)->isHidden(), externalProcessExpectFind.isEmpty());
        QCOMPARE(findWidget(part)->findChild<KLineEdit *>()->text(), externalProcessExpectFind);
        // Checking if the encryption/decryption worked
        QCOMPARE(externalProcessExpectFind, ShellUtils::find(serializedOptions));

    } else if (paths.count() == 2) {
        if (expectTabs) {
            Shell *s = findShell();
            QVERIFY(s);
            QCOMPARE(s->m_tabs.count(), 2);
            Okular::Part *part = dynamic_cast<Okular::Part *>(s->m_tabs[0].part);
            Okular::Part *part2 = dynamic_cast<Okular::Part *>(s->m_tabs[1].part);
            QCOMPARE(part->url(), QUrl::fromLocalFile(paths[0]));
            QCOMPARE(part2->url(), QUrl::fromLocalFile(paths[1]));
            QCOMPARE(partDocument(part)->currentPage(), expectedPage);
            QCOMPARE(partDocument(part2)->currentPage(), expectedPage);
        } else {
            QSet<QString> openUrls;
            Shell *s = findShell();
            QVERIFY(s);
            QCOMPARE(s->m_tabs.count(), 1);
            Okular::Part *part = s->findChild<Okular::Part *>();
            QVERIFY(part);
            QCOMPARE(partDocument(part)->currentPage(), expectedPage);
            openUrls << part->url().url();

            Shell *s2 = findShell(s);
            QVERIFY(s2);
            QCOMPARE(s2->m_tabs.count(), 1);
            Okular::Part *part2 = s2->findChild<Okular::Part *>();
            QVERIFY(part2);
            QCOMPARE(partDocument(part2)->currentPage(), expectedPage);
            openUrls << part2->url().url();

            for (const QString &path : std::as_const(paths)) {
                QVERIFY(openUrls.contains(QUrl::fromLocalFile(path).url()));
            }
        }
    }

    if (!externalProcessPath.isEmpty()) {
        Okular::Part *part = s->findChild<Okular::Part *>();

        QProcess p;
        QStringList args;
        args << externalProcessPath;
        if (unique) {
            args << QStringLiteral("-unique");
        }
        if (externalProcessExpectedPage != 0) {
            args << QStringLiteral("-page") << QString::number(externalProcessExpectedPage + 1);
        }
        if (externalProcessExpectPresentation) {
            args << QStringLiteral("-presentation");
        }
        if (externalProcessExpectPrintDialog) {
            args << QStringLiteral("-print");
        }
#if defined(Q_OS_WIN)
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("OKULAR_TEST_INSTANCE"), QString::number(QCoreApplication::applicationPid()));
        p.setProcessEnvironment(environment);
#endif
        p.start(QStringLiteral(OKULAR_BINARY), args);
        p.waitForStarted();
        QCOMPARE(p.state(), QProcess::Running);

        if (expectTabs || unique) {
            // It is attaching to us, so will eventually stop
            QTRY_COMPARE_WITH_TIMEOUT(p.state(), QProcess::NotRunning, 20000);
            QCOMPARE(p.exitStatus(), QProcess::NormalExit);
            QCOMPARE(p.exitCode(), 0);

            if (unique) {
                // It is unique so part got "overwritten"
                QCOMPARE(s->m_tabs.count(), 1);
                QCOMPARE(part->url(), QUrl::fromLocalFile(externalProcessPath));
                QCOMPARE(partDocument(part)->currentPage(), externalProcessExpectedPage);
            } else {
                // It is attaching to us so a second tab is there
                QCOMPARE(s->m_tabs.count(), 2);
                Okular::Part *part2 = dynamic_cast<Okular::Part *>(s->m_tabs[1].part);
                QCOMPARE(part2->url(), QUrl::fromLocalFile(externalProcessPath));
                QCOMPARE(partDocument(part2)->currentPage(), externalProcessExpectedPage);
            }
        } else {
            QTest::qWait(750);

            // It opened on a new process, so it is still running, we need to kill it
            QCOMPARE(p.state(), QProcess::Running);
            p.terminate();
            p.waitForFinished();
            QVERIFY(p.state() != QProcess::Running);
            // It opened on a new process, so no change for us
            QCOMPARE(s->m_tabs.count(), 1);
            QCOMPARE(part->url(), QUrl::fromLocalFile(paths[0]));
            QCOMPARE(partDocument(part)->currentPage(), externalProcessExpectedPage);
        }
    }

    if (expectPresentation) {
        QCOMPARE(paths.count(), 1);
        Okular::Part *part = s->findChild<Okular::Part *>();
        QTRY_VERIFY(presentationWidget(part) != nullptr);
    }

    if (externalProcessExpectPresentation) {
        Okular::Part *part;
        if (unique) {
            QCOMPARE(s->m_tabs.count(), 1);
            part = dynamic_cast<Okular::Part *>(s->m_tabs[0].part);
        } else {
            QCOMPARE(s->m_tabs.count(), 2);
            part = dynamic_cast<Okular::Part *>(s->m_tabs[1].part);
        }

        QTRY_VERIFY(presentationWidget(part) != nullptr);
    }

    if (helper) {
        QVERIFY(helper->foundDialog);
    }
}

void ClosePrintDialogHelper::closePrintDialog()
{
    Shell *s = findShell();
    QPrintDialog *dialog = s->findChild<QPrintDialog *>();
    if (!dialog) {
        QTimer::singleShot(0, this, &ClosePrintDialogHelper::closePrintDialog);
        return;
    }
    QVERIFY(dialog);
    QCOMPARE(MainShellTest::tabWidget(s)->currentIndex(), m_expectedTab);
    dialog->close();
    foundDialog = true;
}

void MainShellTest::testFileRemembersPagePosition_data()
{
    QTest::addColumn<int>("mode");

    QTest::newRow("normal") << 1;
    QTest::newRow("unique") << 2;
    QTest::newRow("tabs") << 3;
}

void MainShellTest::testFileRemembersPagePosition()
{
    QFETCH(int, mode);

    const QStringList paths = QStringList(QStringLiteral(KDESRCDIR "data/contents.epub"));
    QString serializedOptions;
    if (mode == 1 || mode == 3) {
        serializedOptions = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    } else {
        serializedOptions = ShellUtils::serializeOptions(false, false, false, true, false, QString(), QString(), QString());
    }

    Okular::Settings::self()->setShellOpenFileInTabs(mode == 3);

    Okular::Status status = Okular::main(paths, serializedOptions);
    QCOMPARE(status, Okular::Success);
    Shell *s = findShell();
    QVERIFY(s);
    Okular::Part *part = s->findChild<Okular::Part *>();
    QVERIFY(part);
    QCOMPARE(part->url(), QUrl::fromLocalFile(paths[0]));
    QCOMPARE(partDocument(part)->currentPage(), 0u);
    partDocument(part)->setViewportPage(3);
    QCOMPARE(partDocument(part)->currentPage(), 3u);
    s->closeUrl();
    QCOMPARE(part->url().url(), QString());

    if (mode == 1) {
        delete s;
        status = Okular::main(paths, serializedOptions);
        QCOMPARE(status, Okular::Success);
    } else {
        QProcess p;
        QStringList args;
        args << paths[0];
        if (mode == 2) {
            args << QStringLiteral("-unique");
        }
        p.start(QStringLiteral(OKULAR_BINARY), args);
        p.waitForStarted();
        QCOMPARE(p.state(), QProcess::Running);

        // It is attaching to us, so will eventually stop
        QTRY_COMPARE_WITH_TIMEOUT((int)p.state(), (int)QProcess::NotRunning, 20000);
        QCOMPARE((int)p.exitStatus(), (int)QProcess::NormalExit);
        QCOMPARE(p.exitCode(), 0);
    }
    s = findShell();
    QVERIFY(s);
    part = s->findChild<Okular::Part *>();
    QVERIFY(part);
    QCOMPARE(part->url(), QUrl::fromLocalFile(paths[0]));
    QCOMPARE(partDocument(part)->currentPage(), 3u);
}

void MainShellTest::test2FilesError_data()
{
    QTest::addColumn<QString>("serializedOptions");

    QTest::newRow("startInPresentation") << ShellUtils::serializeOptions(true, false, false, false, false, QString(), QString(), QString());
    QTest::newRow("showPrintDialog") << ShellUtils::serializeOptions(false, true, false, false, false, QString(), QString(), QString());
#if HAVE_DBUS
    QTest::newRow("unique") << ShellUtils::serializeOptions(false, false, false, true, false, QString(), QString(), QString());
    QTest::newRow("pageNumber") << ShellUtils::serializeOptions(false, false, false, false, false, QStringLiteral("3"), QString(), QString());
#endif // HAVE_DBUS
    QTest::newRow("find") << ShellUtils::serializeOptions(false, false, false, false, false, QString(), QStringLiteral("silly"), QString());
}

void MainShellTest::test2FilesError()
{
    QFETCH(QString, serializedOptions);

    QStringList paths;
    paths << QStringLiteral(KDESRCDIR "data/file1.pdf") << QStringLiteral(KDESRCDIR "data/tocreload.pdf");
    Okular::Status status = Okular::main(paths, serializedOptions);
    QCOMPARE(status, Okular::Error);

    Shell *s = findShell();
    QVERIFY(!s);
}

void MainShellTest::testSessionRestore_data()
{
    QTest::addColumn<QStringList>("paths");
    QTest::addColumn<QString>("options");
    QTest::addColumn<bool>("useTabsOpen");
    QTest::addColumn<bool>("useTabsRestore");

    QStringList oneDocPaths(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    QStringList twoDocPaths(oneDocPaths);
    twoDocPaths << QStringLiteral(KDESRCDIR "data/formSamples.pdf");

    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());

    QTest::newRow("1 doc, 1 window, tabs") << oneDocPaths << options << true << true;
    QTest::newRow("2 docs, 1 window, tabs") << twoDocPaths << options << true << true;
    QTest::newRow("2 docs, 2 windows, tabs") << twoDocPaths << options << false << true;
    QTest::newRow("2 docs, 2 windows, no tabs") << twoDocPaths << options << false << false;
    QTest::newRow("2 docs, 1 window, no tabs") << twoDocPaths << options << true << false;
}

void MainShellTest::testSessionRestore()
{
    QFETCH(QStringList, paths);
    QFETCH(QString, options);
    QFETCH(bool, useTabsOpen);
    QFETCH(bool, useTabsRestore);

    Okular::Settings::self()->setShellOpenFileInTabs(useTabsOpen);

    Okular::Status status = Okular::main(paths, options);
    QCOMPARE(status, Okular::Success);

    // Gather some information about the state
    // Verify that the correct number of windows/tabs were opened
    QList<Shell *> shells = getShells();
    QVERIFY(!shells.isEmpty());
    int numDocs = 0;
    for (Shell *shell : std::as_const(shells)) {
        QVERIFY(QTest::qWaitForWindowExposed(shell));
        numDocs += shell->m_tabs.size();
    }

    QCOMPARE(numDocs, paths.size());
    QCOMPARE(shells.size(), useTabsOpen ? 1 : paths.size());
    QTest::qWait(100);

    // Simulate session shutdown. The actual shutdown path comes through
    // QSessionManager XSMP handlers, then KApplication::commitData/saveState,
    // then KMWSessionManager::commitData/saveState. Without simulating an X
    // session manager, the best we can do here is to make a temporary Config
    // and call KMainWindows save functions directly.
    QTemporaryFile configFile;
    QVERIFY(configFile.open());

    int numWindows = 0;
    { // Scope for config so that we can reconstruct from file
        KConfig config(configFile.fileName(), KConfig::SimpleConfig);
        for (Shell *shell : std::as_const(shells)) {
            shell->savePropertiesInternal(&config, ++numWindows);
            // Windows aren't necessarily closed on shutdown, but we'll use
            // this as a way to trigger the destructor code, which is normally
            // connected to the aboutToQuit signal
            shell->close();
        }
    }

    // Wait for shells to delete themselves. QTest::qWait doesn't do deferred
    // deletions so we'll set up a full event loop to do that.
    QEventLoop eventLoop;
    QTimer::singleShot(100, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(QEventLoop::AllEvents);
    // Sometimes the event loop is not enough, so try a bit more to get deferred delete happen
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    shells = getShells();
    QCOMPARE(shells.size(), 0);

    Okular::Settings::self()->setShellOpenFileInTabs(useTabsRestore);

    // Simulate session restore. We can't call KMainWindow::restore() directly
    // because it asks for info from the session manager, which doesn't know
    // about our temporary config. But the logic here mostly mirrors restore().
    KConfig config(configFile.fileName(), KConfig::SimpleConfig);
    for (int i = 1; i <= numWindows; ++i) {
        Shell *shell = new Shell;
        shell->readPropertiesInternal(&config, i);
        shell->show();
    }

    // Verify that the restore state is reasonable
    shells = getShells();
    QVERIFY(!shells.isEmpty());
    numDocs = 0;
    for (Shell *shell : std::as_const(shells)) {
        QVERIFY(QTest::qWaitForWindowExposed(shell));
        numDocs += shell->m_tabs.size();
    }

    QCOMPARE(numDocs, paths.size());
    QCOMPARE(shells.size(), useTabsRestore ? numWindows : paths.size());
}

void MainShellTest::testOpenInvalidFiles_data()
{
    QTest::addColumn<QList<QUrl>>("files");
    QTest::addColumn<QString>("options");

    QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QUrl validFile1 = ShellUtils::urlFromArg(QStringLiteral(KDESRCDIR "data/file1.pdf"), ShellUtils::qfileExistFunc(), QString());
    QUrl validFile2 = ShellUtils::urlFromArg(QStringLiteral(KDESRCDIR "data/file2.pdf"), ShellUtils::qfileExistFunc(), QString());
    QUrl invalidFile = ShellUtils::urlFromArg(QStringLiteral(KDESRCDIR "data/non-existing-doc.pdf"), ShellUtils::qfileExistFunc(), QString());

    QList<QUrl> firstCase {invalidFile, validFile1, validFile2};
    QList<QUrl> secondCase {validFile1, validFile2, invalidFile};

    QTest::newRow("opening the invalid file first") << firstCase << options;
    QTest::newRow("opening the valids file first") << secondCase << options;
}

void MainShellTest::testOpenInvalidFiles()
{
    QFETCH(QList<QUrl>, files);
    QFETCH(QString, options);

    /*
     *  The purpose of this test is to verify that when we open an invalid file, no tab is created in the
     * shell.
     *
     */

    Okular::Settings::self()->setShellOpenFileInTabs(true);
    Okular::Status status = Okular::main(QStringList(), options);
    QCOMPARE(status, Okular::Success);

    Shell *shell = findShell();
    QVERIFY(shell);

    /*
     *  We need to make sure that the KrecentFilesAction is empty before starting, because we will also test that
     * the file gets removed from the recent documents
     *
     */
    shell->m_recent->clear();

    QScopedPointer<TestingUtils::CloseDialogHelper> closeDialogHelper {new TestingUtils::CloseDialogHelper(QDialogButtonBox::StandardButton::Ok)};

    for (const QUrl &file : files) {
        shell->openUrl(file);
    }

    QList<QUrl> recentFiles = shell->m_recent->urls();

    QVERIFY(shell->m_tabs.size() == 2);
    QVERIFY(shell->m_tabWidget->tabBar()->isVisible());

    QVERIFY(!shell->m_tabWidget->tabIcon(0).isNull());
    QVERIFY(!shell->m_tabWidget->tabIcon(1).isNull());

    QVERIFY(recentFiles.size() == 2);
}

void MainShellTest::testCorruptPdfDoesNotDiscardValidTab()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString invalidPath = directory.filePath(QStringLiteral("corrupt.pdf"));
    QFile invalidFile(invalidPath);
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    invalidFile.close();

    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    const QUrl valid = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    shell->openUrl(valid);
    QScopedPointer<TestingUtils::CloseDialogHelper> closeDialogHelper {new TestingUtils::CloseDialogHelper(QDialogButtonBox::StandardButton::Ok)};
    shell->openUrl(QUrl::fromLocalFile(invalidPath));
    QCOMPARE(shell->m_tabs.size(), 1);
    QCOMPARE(shell->m_tabs.first().part->url(), valid);
}

#if defined(Q_OS_WIN)
void MainShellTest::testCorruptPdfWithSeparateWindows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString invalidPath = directory.filePath(QStringLiteral("corrupt.pdf"));
    QFile invalidFile(invalidPath);
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    invalidFile.close();

    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *primary = findShell();
    QVERIFY(primary);
    const QUrl valid = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    primary->openUrl(valid);
    Shell *separate = new Shell(QString(), QString(), true);
    QVERIFY(separate->isValid());
    separate->show();
    separate->openDocumentInTab(valid, QString());
    Shell *detached = new Shell(QString(), QString(), true);
    QVERIFY(detached->isValid());
    detached->show();
    detached->openDocumentInTab(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")), QString());

    QScopedPointer<TestingUtils::CloseDialogHelper> closeDialogHelper {new TestingUtils::CloseDialogHelper(QDialogButtonBox::StandardButton::Ok)};
    primary->openUrl(QUrl::fromLocalFile(invalidPath));
    QCOMPARE(primary->m_tabs.size(), 1);
    QCOMPARE(primary->m_tabs.first().part->url(), valid);
    QCOMPARE(getShells().size(), 3);
}

void MainShellTest::testForwardedCorruptPdfAfterClientDisconnect()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString invalidPath = directory.filePath(QStringLiteral("corrupt.pdf"));
    QFile invalidFile(invalidPath);
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    invalidFile.close();

    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main(QStringList(), options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    const QUrl valid = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    shell->openUrl(valid);
    const QUrl following = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file2.pdf"));

    QLocalSocket launcher;
    launcher.connectToServer(ShellUtils::windowsTabOpenServerName(), QIODevice::ReadWrite);
    QVERIFY(launcher.waitForConnected(1000));
    bool acknowledged = false;
    connect(&launcher, &QLocalSocket::readyRead, &launcher, [&]() {
        acknowledged = launcher.read(1) == QByteArrayLiteral("A");
        launcher.disconnectFromServer();
    });
    const QByteArray payload = tabOpenRequest(0x4f4b5450, options, {invalidPath, following.toLocalFile()});
    QCOMPARE(launcher.write(payload), payload.size());
    launcher.flush();
    QScopedPointer<TestingUtils::CloseDialogHelper> closeDialogHelper {new TestingUtils::CloseDialogHelper(QDialogButtonBox::StandardButton::Ok)};
    QTRY_VERIFY_WITH_TIMEOUT(acknowledged, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(shell->m_tabs.size(), 2, 5000);
    QCOMPARE(shell->m_tabs.first().part->url(), valid);
    QCOMPARE(shell->m_tabs.last().part->url(), following);
}
#endif

void MainShellTest::testOpenTheSameFileSeveralTimes()
{
    QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());

    Okular::Settings::self()->setShellOpenFileInTabs(true);
    Okular::Status status = Okular::main(QStringList(), options);
    QCOMPARE(status, Okular::Success);

    Shell *shell = findShell();
    QVERIFY(shell);

    QUrl file1 = ShellUtils::urlFromArg(QStringLiteral(KDESRCDIR "data/file1.pdf"), ShellUtils::qfileExistFunc(), QString());
    QUrl file2 = ShellUtils::urlFromArg(QStringLiteral(KDESRCDIR "data/file2.pdf"), ShellUtils::qfileExistFunc(), QString());
    QUrl file3 = ShellUtils::urlFromArg(QStringLiteral(KDESRCDIR "data/formattest.pdf"), ShellUtils::qfileExistFunc(), QString());

    shell->openUrl(file1);
    shell->openUrl(file2);
    shell->openUrl(file2);

    QVERIFY(shell->m_tabs.size() == 2);

    QVERIFY(shell->m_tabWidget->currentIndex() == 1);

    Okular::Settings::self()->setSwitchToTabIfOpen(false);

    shell->openUrl(file2);

#if defined(Q_OS_WIN)
    QVERIFY(shell->m_tabs.size() == 2);
    QVERIFY(shell->m_tabWidget->currentIndex() == 1);
#else
    QVERIFY(shell->m_tabs.size() == 3);
#endif

    shell->openUrl(file3);
#if defined(Q_OS_WIN)
    QVERIFY(shell->m_tabWidget->currentIndex() == 2);
    QVERIFY(shell->m_tabs.size() == 3);
    const QUrl differentlyCasedFile2 = QUrl::fromLocalFile(file2.toLocalFile().toUpper());
    shell->openUrl(differentlyCasedFile2);
    QCOMPARE(shell->m_tabs.size(), 3);
    QCOMPARE(shell->m_tabWidget->currentIndex(), 1);
    QVERIFY(shell->m_tabs.at(2).part->closeUrl());
    shell->m_tabWidget->setCurrentIndex(2);
    shell->openUrl(file1);
    QCOMPARE(shell->m_tabs.size(), 3);
    QCOMPARE(shell->m_tabWidget->currentIndex(), 0);
#else
    QVERIFY(shell->m_tabWidget->currentIndex() == 3);

    QVERIFY(shell->m_tabs.size() == 4);
#endif

    Okular::Settings::self()->setSwitchToTabIfOpen(true);
}

void MainShellTest::testMiddleButtonCloseUndo()
{
    const QStringList paths = {QStringLiteral(KDESRCDIR "data/file1.pdf"), QStringLiteral(KDESRCDIR "data/file2.pdf")};
    QString serializedOptions;
    serializedOptions = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());

    Okular::Settings::self()->setShellOpenFileInTabs(true);
    Okular::Status status = Okular::main(paths, serializedOptions);
    QCOMPARE(status, Okular::Success);
    Shell *s = findShell();
    QVERIFY(s);

    QCOMPARE(s->m_tabWidget->count(), paths.size());
    // Close a tab using middle key
    QWidget *firstTab = s->m_tabWidget->tabBar()->tabButton(0, QTabBar::RightSide);
    QVERIFY(firstTab);
    QTest::mouseClick(firstTab, Qt::MiddleButton);
    QCOMPARE(s->m_tabWidget->count(), paths.size() - 1);
    // Undo tab close
    s->undoCloseTab();
    QCOMPARE(s->m_tabWidget->count(), paths.size());
}

QTEST_MAIN(MainShellTest)
#include "mainshelltest.moc"
