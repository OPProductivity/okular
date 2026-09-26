/*
    SPDX-FileCopyrightText: 2002 Wilco Greven <greven@kde.org>
    SPDX-FileCopyrightText: 2002 Chris Cheney <ccheney@cheney.cx>
    SPDX-FileCopyrightText: 2003 Benjamin Meyer <benjamin@csh.rit.edu>
    SPDX-FileCopyrightText: 2003-2004 Christophe Devriese <Christophe.Devriese@student.kuleuven.ac.be>
    SPDX-FileCopyrightText: 2003 Laurent Montel <montel@kde.org>
    SPDX-FileCopyrightText: 2003-2004 Albert Astals Cid <aacid@kde.org>
    SPDX-FileCopyrightText: 2003 Luboš Luňák <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2003 Malcolm Hunter <malcolm.hunter@gmx.co.uk>
    SPDX-FileCopyrightText: 2004 Dominique Devriese <devriese@kde.org>
    SPDX-FileCopyrightText: 2004 Dirk Mueller <mueller@kde.org>

    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2017 Klarälvdalens Datakonsult AB a KDAB Group company <info@kdab.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "shell.h"

// qt/kde includes
#include <KActionCollection>
#include <KConfigGroup>
#include <KIO/Global>
#include <KLocalizedString>
#include <KMessageBox>
#include <KPluginFactory>
#include <KRecentFilesAction>
#include <KSharedConfig>
#include <KStandardAction>
#if !defined(Q_OS_WIN) && !defined(Q_OS_OSX) && !defined(Q_OS_HAIKU)
#include <KStartupInfo>
#include <KWindowInfo>
#endif
#include <KToggleFullScreenAction>
#include <KToolBar>
#include <KUrlMimeData>
#include <KWindowSystem>
#include <KXMLGUIFactory>
#include <QAbstractSocket>
#include <QApplication>
#include <QDebug>
#if HAVE_DBUS
#include <QDBusConnection>
#endif // HAVE_DBUS
#include <QDockWidget>
#include <QDragMoveEvent>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#if defined(Q_OS_WIN)
#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>
#endif
#include <QMenuBar>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

// local includes
#include "../interfaces/viewerinterface.h"
#include "kdocumentviewer.h"
#include "shellutils.h"

#include <algorithm>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

static const char *shouldShowMenuBarComingFromFullScreen = "shouldShowMenuBarComingFromFullScreen";
static const char *shouldShowToolBarComingFromFullScreen = "shouldShowToolBarComingFromFullScreen";

static const char *const SESSION_URL_KEY = "Urls";
static const char *const SESSION_TAB_KEY = "ActiveTab";
static const char *const SESSION_ACTIVE_URL_KEY = "ActiveUrl";
static const char *const SHELL_RESTORE_OPEN_DOCUMENTS_KEY = "ShellRestoreOpenDocuments";
static constexpr int OPEN_DOCUMENT_SESSION_SAVE_DELAY_MS = 2000;
#if defined(Q_OS_WIN)
static constexpr quint32 WINDOWS_TAB_OPEN_MAGIC = 0x4f4b5450; // OKTP
#endif

static constexpr char SIDEBAR_LOCKED_KEY[] = "LockSidebar";
static constexpr char SIDEBAR_VISIBLE_KEY[] = "ShowSidebar";

static inline QString DesktopEntryGroupKey()
{
    return QStringLiteral("Desktop Entry");
}
static inline QString RecentFilesGroupKey()
{
    return QStringLiteral("Recent Files");
}
static inline QString GeneralGroupKey()
{
    return QStringLiteral("General");
}
#if defined(Q_OS_WIN)
static inline QString WindowsTabOpenServerName()
{
    if (QStandardPaths::isTestModeEnabled()) {
        return QStringLiteral("okular-private-tab-open-v1-test-%1").arg(QCoreApplication::applicationPid());
    }
    return QStringLiteral("okular-private-tab-open-v1");
}
#endif
static inline QString OpenDocumentSessionGroupKey()
{
    return QStringLiteral("Shell Open Documents Session");
}

class ResizableStackedWidget : public QStackedWidget
{
    Q_OBJECT

public:
    QSize sizeHint() const override
    {
        return currentWidget()->sizeHint();
    }
    QSize minimumSizeHint() const override
    {
        return currentWidget()->minimumSizeHint();
    }
};

/**
 * Groups sidebar containers in a QDockWidget.
 *
 * This control groups all the sidebar containers provided by each tab (the Part object),
 * allowing the user to dock it to the left and right sides of the window,
 * or detach it from the window altogether.
 */
class Sidebar : public QDockWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr)
        : QDockWidget(parent)
    {
        setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        setFeatures(defaultFeatures());

        m_stackedWidget = new QStackedWidget;
        setWidget(m_stackedWidget);
        // It seems that without requesting a specific minimum size, Qt
        // somehow calculates a (0,-1) minimum size, and then Qt gets angry
        // that negative sizes is not possible.
        setMinimumSize(10, 10);
    }

    bool isLocked() const
    {
        return features().testFlag(NoDockWidgetFeatures);
    }

    void setLocked(bool locked)
    {
        setFeatures(locked ? NoDockWidgetFeatures : defaultFeatures());

        // show titlebar only if not locked
        if (locked) {
            if (!m_dumbTitleWidget) {
                m_dumbTitleWidget = new QWidget;
            }
            setTitleBarWidget(m_dumbTitleWidget);
        } else {
            setTitleBarWidget(nullptr);
        }
    }

    int indexOf(QWidget *widget) const
    {
        return m_stackedWidget->indexOf(widget);
    }

    void addWidget(QWidget *widget)
    {
        m_stackedWidget->addWidget(widget);
    }

    void removeWidget(QWidget *widget)
    {
        m_stackedWidget->removeWidget(widget);
    }

    void setCurrentWidget(QWidget *widget)
    {
        m_stackedWidget->setCurrentWidget(widget);
    }

private:
    static DockWidgetFeatures defaultFeatures()
    {
        DockWidgetFeatures dockFeatures = DockWidgetClosable | DockWidgetMovable;
        if (!KWindowSystem::isPlatformWayland()) { // TODO : Remove this check when QTBUG-87332 is fixed
            dockFeatures |= DockWidgetFloatable;
        }

        return dockFeatures;
    }

    QStackedWidget *m_stackedWidget = nullptr;
    QWidget *m_dumbTitleWidget = nullptr;
};

Shell::Shell(const QString &serializedOptions)
    : KParts::MainWindow()
    , m_menuBarWasShown(true)
    , m_toolBarWasShown(true)
    , m_isValid(true)
{
    setObjectName(QStringLiteral("okular::Shell#"));
    setContextMenuPolicy(Qt::NoContextMenu);
    // otherwise .rc file won't be found by unit test
    setComponentName(QStringLiteral("okular"), QString());
    // set the shell's ui resource file
    setXMLFile(QStringLiteral("shell.rc"));
    m_fileformatsscanned = false;
    m_showMenuBarAction = nullptr;
    // this routine will find and load our Part.  it finds the Part by
    // name which is a bad idea usually.. but it's alright in this
    // case since our Part is made for this Shell

    const auto result = KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("kf6/parts/okularpart")));

    if (!result) {
        // if we couldn't find our Part, we exit since the Shell by
        // itself can't do anything useful
        m_isValid = false;
        KMessageBox::error(this, i18n("Unable to find the Okular component: %1", result.errorString));
        return;
    } else {
        m_partFactory = result.plugin;
    }

    // now that the Part plugin is loaded, create the part
    KParts::ReadWritePart *const firstPart = m_partFactory->create<KParts::ReadWritePart>(this);
    if (firstPart) {
        // Setup the central widget
        m_centralStackedWidget = new ResizableStackedWidget();
        setCentralWidget(m_centralStackedWidget);

        // Setup the welcome screen
        m_welcomeScreen = new WelcomeScreen(this);
        connect(m_welcomeScreen, &WelcomeScreen::openClicked, this, &Shell::fileOpen);
        connect(m_welcomeScreen, &WelcomeScreen::closeClicked, this, &Shell::hideWelcomeScreen);
        connect(m_welcomeScreen, &WelcomeScreen::recentItemClicked, this, [this](const QUrl &url) { openUrl(url); });
        connect(m_welcomeScreen, &WelcomeScreen::forgetRecentItem, this, &Shell::forgetRecentItem);
        m_centralStackedWidget->addWidget(m_welcomeScreen);

        m_welcomeScreen->installEventFilter(this);

        // Setup tab bar
        m_tabWidget = new QTabWidget(this);
        m_tabWidget->setTabsClosable(true);
        m_tabWidget->setElideMode(Qt::ElideRight);
        m_tabWidget->tabBar()->setExpanding(false);
        m_tabWidget->tabBar()->setUsesScrollButtons(true);
        m_tabWidget->tabBar()->setStyleSheet(QStringLiteral("QTabBar::tab { min-width: 128px; max-width: 260px; }"
                                                             "QTabBar QToolButton { background: palette(window); border: 1px solid transparent; border-radius: 2px; padding: 0px; margin: 0px; }"
                                                             "QTabBar QToolButton:hover, QTabBar QToolButton:focus { color: #3daee9; border: 1px solid #3daee9; }"
                                                             "QTabBar QToolButton:pressed { color: #3daee9; border: 1px solid #3daee9; background-color: rgba(61, 174, 233, 35); }"));
        m_tabWidget->setStyleSheet(QStringLiteral("QTabWidget::right-corner { background: palette(window); border: 0px; }"));
        m_tabWidget->setDocumentMode(true);
        m_tabWidget->setMovable(true);

        QWidget *const tabCornerWidget = new QWidget(m_tabWidget);
        tabCornerWidget->setObjectName(QStringLiteral("tabCornerWidget"));
        QHBoxLayout *const tabCornerLayout = new QHBoxLayout(tabCornerWidget);
        tabCornerLayout->setContentsMargins(0, 0, 1, 0);
        tabCornerLayout->setSpacing(1);

        const QString tabCornerButtonStyle = QStringLiteral("QToolButton { border: 1px solid transparent; border-radius: 2px; padding: 0px 0px 2px 0px; }"
                                                            "QToolButton:hover, QToolButton:focus { color: #3daee9; border: 1px solid #3daee9; }"
                                                            "QToolButton:pressed { color: #3daee9; border: 1px solid #3daee9; background-color: rgba(61, 174, 233, 35); }");

        m_centerActiveTabButton = new QToolButton(m_tabWidget->tabBar());
        m_centerActiveTabButton->setText(QStringLiteral("o"));
        m_centerActiveTabButton->setToolTip(i18n("Show Active Tab"));
        m_centerActiveTabButton->setAccessibleName(i18n("Show Active Tab"));
        QFont centerActiveTabButtonFont = m_centerActiveTabButton->font();
        centerActiveTabButtonFont.setPixelSize(13);
        centerActiveTabButtonFont.setBold(false);
        m_centerActiveTabButton->setFont(centerActiveTabButtonFont);
        m_centerActiveTabButton->setFixedSize(16, 26);
        m_centerActiveTabButton->setStyleSheet(tabCornerButtonStyle);
        m_centerActiveTabButton->setAutoRaise(false);
        m_centerActiveTabButton->hide();
        connect(m_centerActiveTabButton, &QToolButton::clicked, this, &Shell::scrollTabBarToCurrentTab);

        m_openTabButton = new QToolButton(tabCornerWidget);
        m_openTabButton->setText(QStringLiteral("+"));
        m_openTabButton->setToolTip(i18n("Open Document"));
        m_openTabButton->setAccessibleName(i18n("Open Document"));
        QFont openTabButtonFont = m_openTabButton->font();
        openTabButtonFont.setPixelSize(20);
        openTabButtonFont.setBold(true);
        m_openTabButton->setFont(openTabButtonFont);
        m_openTabButton->setFixedSize(20, 26);
        m_openTabButton->setStyleSheet(tabCornerButtonStyle);
        m_openTabButton->setAutoRaise(false);
        connect(m_openTabButton, &QToolButton::clicked, this, &Shell::fileOpen);
        tabCornerLayout->addWidget(m_openTabButton);
        tabCornerWidget->setFixedWidth(m_openTabButton->width() + 2);
        m_tabWidget->setCornerWidget(tabCornerWidget, Qt::TopRightCorner);

        m_tabWidget->setAcceptDrops(true);
        m_centralStackedWidget->installEventFilter(this);
        m_tabWidget->installEventFilter(this);
        m_tabWidget->tabBar()->installEventFilter(this);

        m_centralStackedWidget->addWidget(m_tabWidget);

        connect(m_tabWidget, &QTabWidget::currentChanged, this, &Shell::setActiveTab);
        connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &Shell::closeTab);
        connect(m_tabWidget->tabBar(), &QTabBar::tabMoved, this, &Shell::moveTabData);

        m_openDocumentSessionSaveTimer = new QTimer(this);
        m_openDocumentSessionSaveTimer->setSingleShot(true);
        connect(m_openDocumentSessionSaveTimer, &QTimer::timeout, this, &Shell::saveOpenDocumentSession);

        m_sidebar = new Sidebar;
        m_sidebar->setObjectName(QStringLiteral("okular_sidebar"));
        m_sidebar->setContextMenuPolicy(Qt::ActionsContextMenu);
        m_sidebar->setWindowTitle(i18n("Sidebar"));
        connect(m_sidebar, &QDockWidget::visibilityChanged, this, [this](bool visible) {
            // sync sidebar visibility with the m_showSidebarAction only if welcome screen is hidden
            if (m_showSidebarAction && m_centralStackedWidget->currentWidget() != m_welcomeScreen) {
                m_showSidebarAction->setChecked(visible);
            }
            if (m_centralStackedWidget->currentWidget() == m_welcomeScreen) {
                // MainWindow tries hard to make its child dockwidgets shown, but during
                // welcome screen we don't want to see the sidebar,
                // so try a bit more to actually hide it.
                m_sidebar->hide();
            }
            scheduleOpenTabButtonGeometryUpdate();
        });
        m_sidebar->installEventFilter(this);
        addDockWidget(Qt::LeftDockWidgetArea, m_sidebar);

        // then, setup our actions
        setupActions();
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &Shell::saveOpenDocumentSession);
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &QObject::deleteLater);
        // and integrate the part's GUI with the shell's
        setupGUI(Keys | ToolBar | Save);

        // NOTE : apply default sidebar width only after calling setupGUI(...)
        resizeDocks({m_sidebar}, {200}, Qt::Horizontal);

        m_tabs.append(TabState(firstPart));
        m_tabWidget->addTab(firstPart->widget(), QString()); // triggers setActiveTab that calls createGUI( part )
        updateOpenTabButtonGeometry();

        connectPart(firstPart);

        readSettings();

        m_unique = ShellUtils::unique(serializedOptions);
#if HAVE_DBUS
        if (m_unique) {
            m_unique = QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.okular"));
            if (!m_unique) {
                KMessageBox::information(this, i18n("There is already a unique Okular instance running. This instance won't be the unique one."));
            }
        } else {
            // TODO When porting to KF7 Remove
            // PID is not unique in containers and "-" in the name violates D-Bus naming conventions.
            // Was left for compatibility with 3rd-party scripts.
            QString serviceName = QStringLiteral("org.kde.okular-") + QString::number(qApp->applicationPid());
            QDBusConnection::sessionBus().registerService(serviceName);

            QDBusConnection::sessionBus().registerService(ShellUtils::currentProcessDbusName());
        }
        if (ShellUtils::noRaise(serializedOptions)) {
            setAttribute(Qt::WA_ShowWithoutActivating);
        }

        {
            const QString editorCmd = ShellUtils::editorCmd(serializedOptions);
            if (!editorCmd.isEmpty()) {
                QMetaObject::invokeMethod(firstPart, "setEditorCmd", Q_ARG(QString, editorCmd));
            }
        }

        QDBusConnection::sessionBus().registerObject(QStringLiteral("/okularshell"), this, QDBusConnection::ExportScriptableSlots);
#endif // HAVE_DBUS

#if defined(Q_OS_WIN)
        startWindowsTabOpenServer();
#endif

        // Make sure that the welcome scren is visible on startup.
        showWelcomeScreen();
        m_openDocumentSessionReady = true;
    } else {
        m_isValid = false;
        KMessageBox::error(this, i18n("Unable to find the Okular component."));
    }

    connect(guiFactory(), &KXMLGUIFactory::shortcutsSaved, this, &Shell::reloadAllXML);
}

void Shell::reloadAllXML()
{
    for (const TabState &tab : std::as_const(m_tabs)) {
        tab.part->reloadXML();
    }
}

void Shell::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && window()->isFullScreen()) {
        setFullScreen(false);
    }
}

bool Shell::eventFilter(QObject *obj, QEvent *event)
{
    QDragMoveEvent *dmEvent = dynamic_cast<QDragMoveEvent *>(event);
    if (dmEvent) {
        bool accept = dmEvent->mimeData()->hasUrls();
        event->setAccepted(accept);
        return accept;
    }

    QDropEvent *dEvent = dynamic_cast<QDropEvent *>(event);
    if (dEvent) {
        const QList<QUrl> list = KUrlMimeData::urlsFromMimeData(dEvent->mimeData());
        handleDroppedUrls(list);
        dEvent->setAccepted(true);
        return true;
    }

    const bool tabLayoutObject = (m_centralStackedWidget && obj == m_centralStackedWidget) || (m_tabWidget && (obj == m_tabWidget || obj == m_tabWidget->tabBar()))
        || (m_sidebar && obj == m_sidebar);
    const bool tabLayoutEvent = event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::Hide
        || event->type() == QEvent::LayoutRequest || event->type() == QEvent::Move;
    if (tabLayoutObject && tabLayoutEvent) {
        scheduleOpenTabButtonGeometryUpdate();
    }

    if (m_tabWidget && obj == m_tabWidget->tabBar() && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *mEvent = static_cast<QMouseEvent *>(event);
        if (mEvent->button() == Qt::MiddleButton) {
            int tabIndex = m_tabWidget->tabBar()->tabAt(mEvent->pos());
            if (tabIndex != -1) {
                closeTab(tabIndex);
                return true;
            }
        }
    }
    return KParts::MainWindow::eventFilter(obj, event);
}

bool Shell::isValid() const
{
    return m_isValid;
}

void Shell::showOpenRecentMenu()
{
    m_recent->menu()->popup(QCursor::pos());
}

Shell::~Shell()
{
    if (!m_tabs.empty()) {
        writeSettings();
        for (const TabState &tab : std::as_const(m_tabs)) {
            tab.part->closeUrl(false);
        }
        m_tabs.clear();
    }
#if HAVE_DBUS
    if (m_unique) {
        QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.okular"));
    }
#endif // HAVE_DBUS

    delete m_tabWidget;
}

// Open a new document if we have space for it
// This can hang if called on a unique instance and openUrl pops a messageBox
bool Shell::openDocument(const QUrl &url, const QString &serializedOptions)
{
    if (m_tabs.size() <= 0) {
        return false;
    }

    hideWelcomeScreen();

#if !defined(Q_OS_WIN)
    KParts::ReadWritePart *const part = m_tabs[0].part;
    if (!qobject_cast<Okular::ViewerInterface *>(part)->openNewFilesInTabs() && !part->url().isEmpty() && !ShellUtils::unique(serializedOptions)) {
        return false;
    }
#endif

    openUrl(url, serializedOptions);

    return true;
}

bool Shell::openDocument(const QString &urlString, const QString &serializedOptions)
{
    return openDocument(QUrl(urlString), serializedOptions);
}

#if defined(Q_OS_WIN)
void Shell::startWindowsTabOpenServer()
{
    if (m_unique || m_windowsTabOpenServer) {
        return;
    }

    m_windowsTabOpenServer = new QLocalServer(this);
    connect(m_windowsTabOpenServer, &QLocalServer::newConnection, this, &Shell::handleWindowsTabOpenConnection);

    if (m_windowsTabOpenServer->listen(WindowsTabOpenServerName())) {
        return;
    }

    if (m_windowsTabOpenServer->serverError() == QAbstractSocket::AddressInUseError) {
        QLocalSocket probeSocket;
        probeSocket.connectToServer(WindowsTabOpenServerName(), QIODevice::WriteOnly);
        if (probeSocket.waitForConnected(250)) {
            // Another private Okular shell already owns the forwarding endpoint.
            // Do not steal it; otherwise future file opens can target the wrong window.
            probeSocket.disconnectFromServer();
            m_windowsTabOpenServer->deleteLater();
            m_windowsTabOpenServer = nullptr;
            return;
        }

        QLocalServer::removeServer(WindowsTabOpenServerName());
        if (m_windowsTabOpenServer->listen(WindowsTabOpenServerName())) {
            return;
        }
    }

    m_windowsTabOpenServer->deleteLater();
    m_windowsTabOpenServer = nullptr;
}

void Shell::handleWindowsTabOpenConnection()
{
    while (m_windowsTabOpenServer && m_windowsTabOpenServer->hasPendingConnections()) {
        QLocalSocket *socket = m_windowsTabOpenServer->nextPendingConnection();
        socket->setParent(this);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);

        const auto readRequest = [this, socket]() {
            QDataStream stream(socket);
            stream.setVersion(QDataStream::Qt_6_0);
            stream.startTransaction();

            quint32 magic = 0;
            QString serializedOptions;
            QStringList paths;
            stream >> magic >> serializedOptions >> paths;

            if (!stream.commitTransaction()) {
                return;
            }

            if (magic == WINDOWS_TAB_OPEN_MAGIC) {
                const QString page = ShellUtils::page(serializedOptions);
                for (const QString &path : std::as_const(paths)) {
                    if (path == QLatin1String("-")) {
                        continue;
                    }
                    openDocument(ShellUtils::urlFromArg(path, ShellUtils::qfileExistFunc(), page), serializedOptions);
                }

                if (!ShellUtils::noRaise(serializedOptions)) {
                    raisePrivateWindowsShell();
                    QTimer::singleShot(150, this, &Shell::raisePrivateWindowsShell);
                }
            }

            socket->disconnectFromServer();
            socket->deleteLater();
        };

        connect(socket, &QLocalSocket::readyRead, this, readRequest);
        if (socket->bytesAvailable() > 0) {
            readRequest();
        }
    }
}

void Shell::raisePrivateWindowsShell()
{
    if (!isFullScreen() && (isMinimized() || isMaximized())) {
        showNormal();
    } else {
        show();
    }

    // Every regular Windows file launch starts from the same normal-window
    // placement, including launches forwarded to an existing shell.
    applyPrivateWindowsStartupGeometry();

    raise();
    activateWindow();

    QWindow *window = windowHandle();
    if (!window) {
        return;
    }

    KWindowSystem::activateWindow(window);

    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }

    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);

    const DWORD currentThread = GetCurrentThreadId();
    const HWND foregroundWindow = GetForegroundWindow();
    const DWORD foregroundThread = foregroundWindow ? GetWindowThreadProcessId(foregroundWindow, nullptr) : 0;
    const bool attached = foregroundThread && foregroundThread != currentThread && AttachThreadInput(currentThread, foregroundThread, TRUE);

    INPUT input[2] = {};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_MENU;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = VK_MENU;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(INPUT));

    BringWindowToTop(hwnd);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SwitchToThisWindow(hwnd, TRUE);

    if (attached) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
}
#endif

void Shell::openNewlySignedFile(const QString &path, int pageNumber)
{
    // for now, this function just applies the "replace current document"
    // strategy for opening.
    // Given signing is slightly closer to annotating and saving, the over
    // all user experience should not be that different
    // The fact that we get a different file out of saving is a bit of a
    // implementation  detail that shouldn't leak that much onto the users
    QUrl url = QUrl::fromLocalFile(path);
    url.setFragment(QStringLiteral("page=%1").arg(pageNumber));

    const int activeTab = m_tabWidget->currentIndex();
    KParts::ReadWritePart *const activePart = m_tabs[activeTab].part;
    activePart->closeUrl(false);
    activePart->openUrl(url);
}

bool Shell::canOpenDocs(int numDocs, int desktop)
{
    if (m_tabs.size() <= 0 || numDocs <= 0 || m_unique) {
        return false;
    }

#if !defined(Q_OS_WIN)
    KParts::ReadWritePart *const part = m_tabs[0].part;
    const bool allowTabs = qobject_cast<Okular::ViewerInterface *>(part)->openNewFilesInTabs();
    if (!allowTabs && (numDocs > 1 || !part->url().isEmpty())) {
        return false;
    }
#endif

#if !defined(Q_OS_WIN) && !defined(Q_OS_OSX) && !defined(Q_OS_HAIKU)
    const KWindowInfo winfo(window()->effectiveWinId(), NET::WMDesktop);
    if (winfo.desktop() != desktop) {
        return false;
    }
#else
    Q_UNUSED(desktop);
#endif

    return true;
}

void Shell::openUrl(const QUrl &url, const QString &serializedOptions)
{
    hideWelcomeScreen();

    const int activeTab = m_tabWidget->currentIndex();
    KParts::ReadWritePart *const activePart = m_tabs[activeTab].part;
    if (!activePart->url().isEmpty()) {
        if (m_unique) {
            applyOptionsToPart(activePart, serializedOptions);
            if (activePart->openUrl(url)) {
                scheduleOpenDocumentSessionSave();
            }
        } else {
#if defined(Q_OS_WIN)
            openNewTab(url, serializedOptions);
#else
            if (qobject_cast<Okular::ViewerInterface *>(activePart)->openNewFilesInTabs()) {
                openNewTab(url, serializedOptions);
            } else {
                Shell *newShell = new Shell(serializedOptions);
                newShell->show();
                newShell->openUrl(url, serializedOptions);
            }
#endif
        }
    } else {
        m_tabWidget->setTabText(activeTab, url.fileName());
        m_tabWidget->setTabToolTip(activeTab, url.fileName());
        updateOpenTabButtonGeometry();

        applyOptionsToPart(activePart, serializedOptions);
        bool openOk = activePart->openUrl(url);
        const bool isstdin = url.fileName() == QLatin1String("-") || url.scheme() == QLatin1String("fd");
        if (!isstdin) {
            if (openOk) {
                setActiveTab(activeTab);
                m_recent->addUrl(url);
                scheduleOpenDocumentSessionSave();
            } else {
                m_recent->removeUrl(url);
                closeTab(activeTab);
            }
        }
    }
}

void Shell::closeUrl()
{
    closeTab(m_tabWidget->currentIndex());

    // When closing the current tab two things can happen:
    //  * the focus was on the tab
    //  * the focus was somewhere in the toolbar
    // we don't have other places that accept focus
    //  * If it was on the tab, logic says it should go back to the next current tab
    //  * If it was on the toolbar, we could leave it there, but since we redo the menus/toolbars for the new tab, it gets kind of lost
    //    so it's easier to set it to the next current tab which also makes sense as consistency
    if (m_tabWidget->count() >= 0) {
        KParts::ReadWritePart *const newPart = m_tabs[m_tabWidget->currentIndex()].part;
        newPart->widget()->setFocus();
    }
}

void Shell::readSettings()
{
    readRecentFilesSettings();

    const KConfigGroup group = KSharedConfig::openConfig()->group(DesktopEntryGroupKey());
    bool fullScreen = group.readEntry("FullScreen", false);
    setFullScreen(fullScreen);

    if (fullScreen) {
        m_menuBarWasShown = group.readEntry(shouldShowMenuBarComingFromFullScreen, true);
        m_toolBarWasShown = group.readEntry(shouldShowToolBarComingFromFullScreen, true);
    }

    const KConfigGroup sidebarGroup = KSharedConfig::openConfig()->group(GeneralGroupKey());
    m_sidebar->setVisible(sidebarGroup.readEntry(SIDEBAR_VISIBLE_KEY, true));
    m_sidebar->setLocked(sidebarGroup.readEntry(SIDEBAR_LOCKED_KEY, true));

    m_showSidebarAction->setChecked(m_sidebar->isVisibleTo(this));
    m_lockSidebarAction->setChecked(m_sidebar->isLocked());
}

void Shell::writeSettings()
{
    saveRecents();

    KConfigGroup sidebarGroup = KSharedConfig::openConfig()->group(GeneralGroupKey());
    sidebarGroup.writeEntry(SIDEBAR_LOCKED_KEY, m_sidebar->isLocked());
    // NOTE : Consider whether the m_showSidebarAction is checked, because
    // the sidebar can be forcibly hidden if the welcome screen is displayed
    sidebarGroup.writeEntry(SIDEBAR_VISIBLE_KEY, m_sidebar->isVisibleTo(this) || m_showSidebarAction->isChecked());

    KConfigGroup group = KSharedConfig::openConfig()->group(DesktopEntryGroupKey());
    group.writeEntry("FullScreen", m_fullScreenAction->isChecked());
    if (m_fullScreenAction->isChecked()) {
        group.writeEntry(shouldShowMenuBarComingFromFullScreen, m_menuBarWasShown);
        group.writeEntry(shouldShowToolBarComingFromFullScreen, m_toolBarWasShown);
    }
    KSharedConfig::openConfig()->sync();
}

void Shell::saveRecents()
{
    m_recent->saveEntries(KSharedConfig::openConfig()->group(RecentFilesGroupKey()));
}

void Shell::setupActions()
{
    KStandardAction::open(this, SLOT(fileOpen()), actionCollection());
    m_recent = KStandardAction::openRecent(this, SLOT(openUrl(QUrl)), actionCollection());
    m_recent->setToolBarMode(KRecentFilesAction::MenuMode);
    connect(m_recent, &QAction::triggered, this, &Shell::showOpenRecentMenu);
    connect(m_recent, &KRecentFilesAction::recentListCleared, this, &Shell::refreshRecentsOnWelcomeScreen);
    connect(m_welcomeScreen, &WelcomeScreen::forgetAllRecents, m_recent, &KRecentFilesAction::clear);
    m_recent->setToolTip(i18n("Click to open a file\nClick and hold to open a recent file"));
    m_recent->setWhatsThis(i18n("<b>Click</b> to open a file or <b>Click and hold</b> to select a recent file"));
    m_printAction = KStandardAction::print(this, SLOT(print()), actionCollection());
    m_printAction->setEnabled(false);
    m_closeAction = KStandardAction::close(this, SLOT(closeUrl()), actionCollection());
    m_closeAction->setEnabled(false);
    KStandardAction::quit(this, SLOT(close()), actionCollection());

    setStandardToolBarMenuEnabled(true);

    m_showMenuBarAction = KStandardAction::showMenubar(this, SLOT(slotShowMenubar()), actionCollection());
    m_fullScreenAction = KStandardAction::fullScreen(this, SLOT(slotUpdateFullScreen()), this, actionCollection());

    actionCollection()->setDefaultShortcuts(m_fullScreenAction, KStandardShortcut::fullScreen() + QList {QKeySequence(Qt::Key_F11)});

    m_nextTabAction = actionCollection()->addAction(QStringLiteral("tab-next"));
    m_nextTabAction->setText(i18n("Next Tab"));
    actionCollection()->setDefaultShortcuts(m_nextTabAction, KStandardShortcut::tabNext());
    m_nextTabAction->setEnabled(false);
    connect(m_nextTabAction, &QAction::triggered, this, &Shell::activateNextTab);

    m_prevTabAction = actionCollection()->addAction(QStringLiteral("tab-previous"));
    m_prevTabAction->setText(i18n("Previous Tab"));
    actionCollection()->setDefaultShortcuts(m_prevTabAction, KStandardShortcut::tabPrev());
    m_prevTabAction->setEnabled(false);
    connect(m_prevTabAction, &QAction::triggered, this, &Shell::activatePrevTab);

    // add shortcuts for Shift+Alt+1 to Shift+Alt+9 to switch tabs(browser logic- Shift+Alt+9 is always going to be last tab)
    for (int i = 1; i <= 9; ++i) {
        QAction *action = actionCollection()->addAction(QStringLiteral("tab-switch-%1").arg(i));
        action->setText(i18n("Switch to Tab %1", i));

        // static cast to Qt::Key satisfies Qt 6 strict type checking for QKeySequence
        actionCollection()->setDefaultShortcut(action, QKeySequence(Qt::SHIFT | Qt::ALT | static_cast<Qt::Key>(Qt::Key_0 + i)));
        connect(action, &QAction::triggered, this, [this, i]() {
            if (m_tabs.isEmpty()) {
                return;
            }
            int index = (i == 9) ? m_tabs.size() - 1 : i - 1;
            if (index >= 0 && index < m_tabs.size()) {
                setActiveTab(index);
            }
        });
    }

    m_undoCloseTab = actionCollection()->addAction(QStringLiteral("undo-close-tab"));
    m_undoCloseTab->setText(i18n("Undo close tab"));
    actionCollection()->setDefaultShortcut(m_undoCloseTab, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    m_undoCloseTab->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
    m_undoCloseTab->setEnabled(false);
    connect(m_undoCloseTab, &QAction::triggered, this, &Shell::undoCloseTab);

    m_lockSidebarAction = actionCollection()->addAction(QStringLiteral("okular_lock_sidebar"));
    m_lockSidebarAction->setCheckable(true);
    m_lockSidebarAction->setIcon(QIcon::fromTheme(QStringLiteral("lock")));
    m_lockSidebarAction->setText(i18n("Lock Sidebar"));
    connect(m_lockSidebarAction, &QAction::triggered, m_sidebar, &Sidebar::setLocked);
    m_sidebar->addAction(m_lockSidebarAction);
}

void Shell::saveProperties(KConfigGroup &group)
{
    if (!m_isValid) { // part couldn't be loaded, nothing to save
        return;
    }

    writeOpenDocumentSession(group);
}

void Shell::readProperties(const KConfigGroup &group)
{
    // Reopen documents based on saved settings
    QStringList urls = group.readPathEntry(SESSION_URL_KEY, QStringList());
    int desiredTab = group.readEntry<int>(SESSION_TAB_KEY, 0);

    while (!urls.isEmpty()) {
        openUrl(QUrl(urls.takeFirst()));
    }

    if (desiredTab < m_tabs.size()) {
        setActiveTab(desiredTab);
    }
}

bool Shell::openDocumentSessionRestoreEnabled() const
{
    const KConfigGroup group = KSharedConfig::openConfig()->group(GeneralGroupKey());
    return group.readEntry(SHELL_RESTORE_OPEN_DOCUMENTS_KEY, true);
}

void Shell::writeOpenDocumentSession(KConfigGroup &group) const
{
    QStringList urls;
    int activeTab = 0;
    QUrl activeUrl;
    const int currentTab = m_tabWidget->currentIndex();

    for (int i = 0; i < m_tabs.size(); ++i) {
        const QUrl url = m_tabs[i].part->url();
        if (url.isEmpty()) {
            continue;
        }

        if (i == currentTab) {
            activeTab = urls.size();
            activeUrl = url;
        }
        urls.append(url.url());
    }

    if (activeUrl.isEmpty() && !urls.isEmpty()) {
        activeUrl = QUrl(urls.value(activeTab));
    }

    group.writePathEntry(SESSION_URL_KEY, urls);
    group.writeEntry(SESSION_TAB_KEY, activeTab);
    group.writePathEntry(SESSION_ACTIVE_URL_KEY, activeUrl.url());
}

void Shell::scheduleOpenDocumentSessionSave()
{
    if (!m_openDocumentSessionReady || m_restoringOpenDocumentSession || !openDocumentSessionRestoreEnabled()) {
        return;
    }

    m_openDocumentSessionSaveTimer->start(OPEN_DOCUMENT_SESSION_SAVE_DELAY_MS);
}

void Shell::saveOpenDocumentSession()
{
    if (!m_openDocumentSessionReady || m_restoringOpenDocumentSession || !openDocumentSessionRestoreEnabled()) {
        return;
    }

    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup group = config->group(OpenDocumentSessionGroupKey());
    writeOpenDocumentSession(group);
    config->sync();
}

bool Shell::restoreOpenDocumentSession()
{
    if (!m_isValid || !openDocumentSessionRestoreEnabled()) {
        return false;
    }

    const KConfigGroup group = KSharedConfig::openConfig()->group(OpenDocumentSessionGroupKey());
    const QStringList urls = group.readPathEntry(SESSION_URL_KEY, QStringList());
    if (urls.isEmpty()) {
        return false;
    }

    const int desiredTab = group.readEntry<int>(SESSION_TAB_KEY, 0);
    const QUrl desiredUrl(group.readPathEntry(SESSION_ACTIVE_URL_KEY, QString()));

    bool restoredAny = false;
    int targetTab = -1;

    m_restoringOpenDocumentSession = true;
    for (int oldIndex = 0; oldIndex < urls.size(); ++oldIndex) {
        const QUrl url(urls.at(oldIndex));
        if (!url.isValid() || url.isEmpty()) {
            qWarning() << "Skipping invalid Okular open document session URL:" << urls.at(oldIndex);
            continue;
        }

        if (!url.isLocalFile()) {
            qWarning() << "Skipping non-local Okular open document session URL for v1:" << url;
            continue;
        }

        if (!QFile::exists(url.toLocalFile())) {
            qWarning() << "Skipping missing Okular open document session file:" << url.toLocalFile();
            continue;
        }

        openUrl(url);
        const int openedTab = findTabIndex(url);
        if (openedTab < 0) {
            qWarning() << "Failed to restore Okular open document session URL:" << url;
            continue;
        }

        restoredAny = true;
        if (oldIndex == desiredTab || (!desiredUrl.isEmpty() && url == desiredUrl)) {
            targetTab = openedTab;
        }
    }
    m_restoringOpenDocumentSession = false;

    if (restoredAny) {
        if (targetTab < 0 || targetTab >= m_tabs.size()) {
            targetTab = 0;
        }
        setActiveTab(targetTab);
    }

    saveOpenDocumentSession();
    return restoredAny;
}

void Shell::fileOpen()
{
    // this slot is called whenever the File->Open menu is selected,
    // the Open shortcut is pressed (usually CTRL+O) or the Open toolbar
    // button is clicked
    const int activeTab = m_tabWidget->currentIndex();
    if (!m_fileformatsscanned) {
        const KDocumentViewer *const doc = qobject_cast<KDocumentViewer *>(m_tabs[activeTab].part);
        Q_ASSERT(doc);

        m_fileformats = doc->supportedMimeTypes();

        m_fileformatsscanned = true;
    }

    QUrl startDir;
    const KParts::ReadWritePart *const curPart = m_tabs[activeTab].part;
    if (curPart->url().isLocalFile()) {
        startDir = KIO::upUrl(curPart->url());
    }
    if (startDir.isEmpty() || (startDir == QUrl::fromLocalFile(QDir::rootPath()))) {
        startDir = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }

    QPointer<QFileDialog> dlg(new QFileDialog(this));
    dlg->setDirectoryUrl(startDir);
    dlg->setAcceptMode(QFileDialog::AcceptOpen);
    dlg->setOption(QFileDialog::HideNameFilterDetails, true);
    dlg->setFileMode(QFileDialog::ExistingFiles); // Allow selection of more than one file

    QMimeDatabase mimeDatabase;
    // Unfortunately non Plasma file dialogs don't support the "All supported files" when using
    // setMimeTypeFilters instead of setNameFilters, so for those use setNameFilters which is a bit
    // worse because doesn't show you pdf files named bla.blo when you say "show me the pdf files", but
    // that's solvable by choosing "All Files" and it's not that common while it's more convenient to
    // only get shown the files that the application can open by default instead of all of them
    const bool useMimeTypeFilters = qgetenv("XDG_CURRENT_DESKTOP").toLower() == "kde";
    if (useMimeTypeFilters) {
        QStringList mimetypes;
        for (const QString &mimeName : std::as_const(m_fileformats)) {
            QMimeType mimeType = mimeDatabase.mimeTypeForName(mimeName);
            mimetypes << mimeType.name();
        }
        mimetypes.prepend(QStringLiteral("application/octet-stream"));
        dlg->setMimeTypeFilters(mimetypes);
    } else {
        QSet<QString> globPatterns;
        QMap<QString, QStringList> namedGlobs;
        for (const QString &mimeName : std::as_const(m_fileformats)) {
            QMimeType mimeType = mimeDatabase.mimeTypeForName(mimeName);
            const QStringList globs(mimeType.globPatterns());
            if (globs.isEmpty()) {
                continue;
            }

            globPatterns.unite(QSet<QString>(globs.begin(), globs.end()));

            namedGlobs[mimeType.comment()].append(globs);
        }
        QStringList namePatterns;
        for (auto it = namedGlobs.cbegin(); it != namedGlobs.cend(); ++it) {
            namePatterns.append(it.key() + QLatin1String(" (") + it.value().join(QLatin1Char(' ')) + QLatin1Char(')'));
        }

        const QStringList allGlobPatterns = globPatterns.values();
        namePatterns.prepend(i18n("All files (*)"));
        namePatterns.prepend(i18n("All supported files (%1)", allGlobPatterns.join(QLatin1Char(' '))));
        dlg->setNameFilters(namePatterns);
    }

    dlg->setWindowTitle(i18n("Open Document")); /* cppcheck-suppress nullPointerRedundantCheck ; QPointer things here is not understood*/
    if (dlg->exec() && dlg) {                   /* cppcheck-suppress nullPointerRedundantCheck ; QPointer things here is not understood*/
        const QList<QUrl> urlList = dlg->selectedUrls();
        for (const QUrl &url : urlList) {
            openUrl(url);
        }
    }

    if (dlg) {
        delete dlg.data();
    }
}

void Shell::tryRaise(const QString &startupId)
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_OSX) && !defined(Q_OS_HAIKU)
    if (KWindowSystem::isPlatformWayland()) {
        KWindowSystem::setCurrentXdgActivationToken(startupId);
    } else if (KWindowSystem::isPlatformX11()) {
        KStartupInfo::setNewStartupId(window()->windowHandle(), startupId.toUtf8());
    }
#else
    Q_UNUSED(startupId);
#endif

    KWindowSystem::activateWindow(window()->windowHandle());
}

// only called when starting the program
void Shell::setFullScreen(bool useFullScreen)
{
    if (useFullScreen) {
        setWindowState(windowState() | Qt::WindowFullScreen); // set
    } else {
        setWindowState(windowState() & ~Qt::WindowFullScreen); // reset
    }
}

void Shell::setCaption(const QString &caption)
{
    bool modified = false;

    const int activeTab = m_tabWidget->currentIndex();
    if (activeTab != -1) {
        KParts::ReadWritePart *const activePart = m_tabs[activeTab].part;
        QString tabCaption = activePart->url().fileName();
        if (activePart->isModified()) {
            modified = true;
            if (!tabCaption.isEmpty()) {
                tabCaption.append(QStringLiteral(" *"));
            }
        }

        m_tabWidget->setTabText(activeTab, tabCaption);
    }

    setCaption(caption, modified);
}

void Shell::showEvent(QShowEvent *e)
{
    if (!menuBar()->isNativeMenuBar() && m_showMenuBarAction) {
        m_showMenuBarAction->setChecked(menuBar()->isVisible());
    }

    KParts::MainWindow::showEvent(e);

#if defined(Q_OS_WIN)
    if (!m_privateWindowsStartupGeometryApplied && !isFullScreen()) {
        m_privateWindowsStartupGeometryApplied = true;
        applyPrivateWindowsStartupGeometry();
        QTimer::singleShot(0, this, &Shell::applyPrivateWindowsStartupGeometry);
        QTimer::singleShot(150, this, &Shell::applyPrivateWindowsStartupGeometry);
    }
#endif
}

void Shell::resizeEvent(QResizeEvent *e)
{
    KParts::MainWindow::resizeEvent(e);
    scheduleOpenTabButtonGeometryUpdate();
}

#if defined(Q_OS_WIN)
void Shell::applyPrivateWindowsStartupGeometry()
{
    if (isFullScreen()) {
        return;
    }

    if (isMinimized() || isMaximized()) {
        showNormal();
    }

    const HWND hwnd = reinterpret_cast<HWND>(winId());
    MONITORINFO monitorInfo = {sizeof(MONITORINFO)};
    const HMONITOR primaryMonitor = MonitorFromPoint(POINT {0, 0}, MONITOR_DEFAULTTOPRIMARY);
    RECT windowRect = {};
    if (hwnd && primaryMonitor && GetMonitorInfoW(primaryMonitor, &monitorInfo) && GetWindowRect(hwnd, &windowRect)) {
        RECT visibleRect = windowRect;
        const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        using DwmGetWindowAttributeFunction = HRESULT(WINAPI *)(HWND, DWORD, PVOID, DWORD);
        const auto getWindowAttribute = dwm ? reinterpret_cast<DwmGetWindowAttributeFunction>(GetProcAddress(dwm, "DwmGetWindowAttribute")) : nullptr;
        if (getWindowAttribute) {
            RECT dwmRect = {};
            if (SUCCEEDED(getWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &dwmRect, sizeof(dwmRect))) && dwmRect.right > dwmRect.left && dwmRect.bottom > dwmRect.top) {
                visibleRect = dwmRect;
            }
        }
        if (dwm) {
            FreeLibrary(dwm);
        }

        const int leftInset = visibleRect.left - windowRect.left;
        const int topInset = visibleRect.top - windowRect.top;
        const int rightInset = windowRect.right - visibleRect.right;
        const int bottomInset = windowRect.bottom - visibleRect.bottom;
        const RECT screenRect = monitorInfo.rcMonitor;
        if (SetWindowPos(hwnd,
                         nullptr,
                         screenRect.left - leftInset,
                         screenRect.top - topInset,
                         screenRect.right - screenRect.left + leftInset + rightInset,
                         screenRect.bottom - screenRect.top + topInset + bottomInset,
                         SWP_NOZORDER | SWP_NOACTIVATE)) {
            return;
        }
    }

    QScreen *targetScreen = QGuiApplication::primaryScreen();
    if (!targetScreen) {
        targetScreen = screen();
    }
    if (!targetScreen) {
        return;
    }

    const QRect screenGeometry = targetScreen->geometry();
    const QRect frame = frameGeometry();
    const QRect client = geometry();

    const int leftFrame = client.left() - frame.left();
    const int topFrame = client.top() - frame.top();
    const int rightFrame = frame.right() - client.right();
    const int bottomFrame = frame.bottom() - client.bottom();

    const int width = std::max(1, screenGeometry.width() - leftFrame - rightFrame);
    const int height = std::max(1, screenGeometry.height() - topFrame - bottomFrame);

    setGeometry(screenGeometry.left() + leftFrame, screenGeometry.top() + topFrame, width, height);
}
#endif

void Shell::slotUpdateFullScreen()
{
    if (m_fullScreenAction->isChecked()) {
        m_menuBarWasShown = !menuBar()->isHidden();
        menuBar()->hide();

        m_toolBarWasShown = !toolBar()->isHidden();
        toolBar()->hide();

        KToggleFullScreenAction::setFullScreen(this, true);
    } else {
        if (m_menuBarWasShown) {
            menuBar()->show();
        }
        if (m_toolBarWasShown) {
            toolBar()->show();
        }
        KToggleFullScreenAction::setFullScreen(this, false);
    }
}

void Shell::slotShowMenubar()
{
    if (menuBar()->isHidden()) {
        menuBar()->show();
    } else {
        menuBar()->hide();
    }
}

QSize Shell::sizeHint() const
{
    const QSize baseSize = QApplication::primaryScreen()->availableSize() * 0.6;
    // Set an arbitrary yet sensible sane minimum size for very small screens;
    // for example we don't want people using 1366x768 screens to get a tiny
    // default window size of 820 x 460 which will elide most of the toolbar buttons.
    return baseSize.expandedTo(QSize(1000, 700));
}

bool Shell::queryClose()
{
    if (m_tabs.count() > 1) {
        const QString dontAskAgainName = QStringLiteral("ShowTabWarning");
        KMessageBox::ButtonCode dummy = KMessageBox::PrimaryAction;
        if (KMessageBox::shouldBeShownTwoActions(dontAskAgainName, dummy)) {
            QDialog *dialog = new QDialog(this);
            dialog->setWindowTitle(i18n("Confirm Close"));

            QDialogButtonBox *buttonBox = new QDialogButtonBox(dialog);
            buttonBox->setStandardButtons(QDialogButtonBox::Yes | QDialogButtonBox::No);
            KGuiItem::assign(buttonBox->button(QDialogButtonBox::Yes), KGuiItem(i18n("Close Tabs"), QStringLiteral("tab-close")));
            KGuiItem::assign(buttonBox->button(QDialogButtonBox::No), KStandardGuiItem::cancel());

            bool checkboxResult = true;
            const int result = KMessageBox::createKMessageBox(dialog,
                                                              buttonBox,
                                                              QMessageBox::Question,
                                                              i18n("You are about to close %1 tabs. Are you sure you want to continue?", m_tabs.count()),
                                                              QStringList(),
                                                              i18n("Warn me when I attempt to close multiple tabs"),
                                                              &checkboxResult,
                                                              KMessageBox::Notify);

            if (!checkboxResult) {
                KMessageBox::saveDontShowAgainTwoActions(dontAskAgainName, dummy);
            }

            if (result != QDialogButtonBox::Yes) {
                return false;
            }
        }
    }

    for (int i = 0; i < m_tabs.size(); ++i) {
        KParts::ReadWritePart *const part = m_tabs[i].part;

        // To resolve confusion about multiple modified docs, switch to relevant tab
        if (part->isModified()) {
            setActiveTab(i);
        }

        if (!part->queryClose()) {
            return false;
        }
    }
    return true;
}

void Shell::setActiveTab(int tab)
{
    if (tab < 0 || tab >= m_tabs.size()) {
        return;
    }

    if (m_showSidebarAction) {
        m_showSidebarAction->disconnect(m_sidebar);
    }

    m_tabWidget->setCurrentIndex(tab);

    // NOTE : createGUI(...) breaks the visibility of the sidebar, so we need
    // to save and restore it
    const bool isSidebarVisible = m_sidebar->isVisible();
    createGUI(m_tabs[tab].part);
    m_sidebar->setVisible(isSidebarVisible);

    // dock KPart's sidebar if new and make it current
    Okular::ViewerInterface *iPart = qobject_cast<Okular::ViewerInterface *>(m_tabs[tab].part);
    Q_ASSERT(iPart);
    QWidget *sideContainer = iPart->getSideContainer();
    if (m_sidebar->indexOf(sideContainer) == -1) {
        m_sidebar->addWidget(sideContainer);
        if (m_sidebar->maximumWidth() > sideContainer->maximumWidth()) {
            m_sidebar->setMaximumWidth(sideContainer->maximumWidth());
        }
    }
    m_sidebar->setCurrentWidget(sideContainer);
    if (m_sidebar->isVisible()) {
        QTimer::singleShot(0, this, [this]() {
            if (m_sidebar && m_sidebar->isVisible() && m_sidebar->width() < 180) {
                resizeDocks({m_sidebar}, {220}, Qt::Horizontal);
            }
            scheduleOpenTabButtonGeometryUpdate();
        });
    }

    m_showSidebarAction = m_tabs[tab].part->actionCollection()->action(QStringLiteral("show_leftpanel"));
    Q_ASSERT(m_showSidebarAction);
    m_showSidebarAction->disconnect(m_sidebar);
    m_showSidebarAction->setChecked(m_sidebar->isVisibleTo(this));
    connect(m_showSidebarAction, &QAction::triggered, m_sidebar, &Sidebar::setVisible);

    m_printAction->setEnabled(m_tabs[tab].printEnabled);
    m_closeAction->setEnabled(m_tabs[tab].closeEnabled);
    if (tab == 0) {
        QTimer::singleShot(0, this, &Shell::resetTabBarScrollToStart);
    } else {
        QTimer::singleShot(0, this, &Shell::updateOpenTabButtonGeometry);
    }
    scheduleOpenDocumentSessionSave();
}

void Shell::closeTab(int tab)
{
    KParts::ReadWritePart *const part = m_tabs[tab].part;
    QUrl url = part->url();
    bool closeSuccess = part->closeUrl();
    if (closeSuccess && m_tabs.count() > 1) {
        if (part->factory()) {
            part->factory()->removeClient(part);
        }
        part->disconnect(this);

        Okular::ViewerInterface *iPart = qobject_cast<Okular::ViewerInterface *>(m_tabs[tab].part);
        Q_ASSERT(iPart);
        QWidget *sideContainer = iPart->getSideContainer();
        m_sidebar->removeWidget(sideContainer);
        connect(part, &QObject::destroyed, sideContainer, &QObject::deleteLater);

        part->deleteLater();
        m_tabs.removeAt(tab);
        m_tabWidget->removeTab(tab);
        updateOpenTabButtonGeometry();
        m_undoCloseTab->setEnabled(true);
        m_closedTabUrls.append(url);

        if (m_tabWidget->count() == 1) {
            m_nextTabAction->setEnabled(false);
            m_prevTabAction->setEnabled(false);
        }
    } else if (closeSuccess && m_tabs.count() == 1) {
        // Show welcome screen when the last tab is closed.

        showWelcomeScreen();
    }

    if (closeSuccess) {
        scheduleOpenDocumentSessionSave();
    }
}

void Shell::openNewTab(const QUrl &url, const QString &serializedOptions)
{
    const int previousActiveTab = m_tabWidget->currentIndex();
    KParts::ReadWritePart *const activePart = m_tabs[previousActiveTab].part;

    hideWelcomeScreen();

    bool activateTabIfAlreadyOpen;
    QMetaObject::invokeMethod(activePart, "activateTabIfAlreadyOpenFile", Q_RETURN_ARG(bool, activateTabIfAlreadyOpen));

    if (activateTabIfAlreadyOpen) {
        const int tabIndex = findTabIndex(url);

        if (tabIndex >= 0) {
            setActiveTab(tabIndex);
            m_recent->addUrl(url);
            return;
        }
    }

    if (m_tabs.size() == 1) {
        m_nextTabAction->setEnabled(true);
        m_prevTabAction->setEnabled(true);
    }

    const int newIndex = m_tabs.size();

    // Make new part
    m_tabs.append(TabState(m_partFactory->create<KParts::ReadWritePart>(this)));
    connectPart(m_tabs[newIndex].part);

    // Update GUI
    KParts::ReadWritePart *const part = m_tabs[newIndex].part;
    m_tabWidget->addTab(part->widget(), url.fileName());
    m_tabWidget->setTabToolTip(newIndex, url.fileName());
    updateOpenTabButtonGeometry();

    applyOptionsToPart(part, serializedOptions);

    setActiveTab(m_tabs.size() - 1);

    if (part->openUrl(url)) {
        m_recent->addUrl(url);
        scheduleOpenDocumentSessionSave();
    } else {
        setActiveTab(previousActiveTab);
        closeTab(m_tabs.size() - 1);
        m_recent->removeUrl(url);
    }
}

void Shell::applyOptionsToPart(QObject *part, const QString &serializedOptions)
{
    KDocumentViewer *const doc = qobject_cast<KDocumentViewer *>(part);
    const QString find = ShellUtils::find(serializedOptions);
    if (ShellUtils::startInPresentation(serializedOptions)) {
        doc->startPresentation();
    }
    if (ShellUtils::showPrintDialog(serializedOptions)) {
        QMetaObject::invokeMethod(part, "enableStartWithPrint");
    }
    if (ShellUtils::showPrintDialogAndExit(serializedOptions)) {
        QMetaObject::invokeMethod(part, "enableExitAfterPrint");
    }
    if (!find.isEmpty()) {
        QMetaObject::invokeMethod(part, "enableStartWithFind", Q_ARG(QString, find));
    }
}

void Shell::connectPart(const KParts::ReadWritePart *part)
{
    // NOLINTBEGIN(clazy-old-style-connect);
    // We're abusing the fact we know the part is our part here
    connect(this, SIGNAL(moveSplitter(int)), part, SLOT(moveSplitter(int)));
    connect(part, SIGNAL(enablePrintAction(bool)), this, SLOT(setPrintEnabled(bool)));
    connect(part, SIGNAL(enableCloseAction(bool)), this, SLOT(setCloseEnabled(bool)));
    connect(part, SIGNAL(mimeTypeChanged(QMimeType)), this, SLOT(setTabIcon(QMimeType)));
    connect(part, SIGNAL(urlsDropped(QList<QUrl>)), this, SLOT(handleDroppedUrls(QList<QUrl>)));
    connect(part, SIGNAL(maxRecentItemsChanged(int)), this, SLOT(triggerUpdateRecentItems(int)));
    connect(part, SIGNAL(documentSaveFinished(QUrl)), this, SLOT(scheduleOpenDocumentSessionSave()));

    // clang-format off
    // Formatting disabled to keep signature normalized
    connect(part, SIGNAL(requestOpenNewlySignedFile(QString,int)), this, SLOT(openNewlySignedFile(QString,int)));
    connect(part, SIGNAL(fitWindowToPage(QSize,QSize)), this, SLOT(slotFitWindowToPage(QSize,QSize)));
    // clang-format on
    // NOLINTEND(clazy-old-style-connect);
}

void Shell::print()
{
    QMetaObject::invokeMethod(m_tabs[m_tabWidget->currentIndex()].part, "slotPrint");
}

void Shell::setPrintEnabled(bool enabled)
{
    int i = findTabIndex(sender());
    if (i != -1) {
        m_tabs[i].printEnabled = enabled;
        if (i == m_tabWidget->currentIndex()) {
            m_printAction->setEnabled(enabled);
        }
    }
}

void Shell::setCloseEnabled(bool enabled)
{
    int i = findTabIndex(sender());
    if (i != -1) {
        m_tabs[i].closeEnabled = enabled;
        if (i == m_tabWidget->currentIndex()) {
            m_closeAction->setEnabled(enabled);
        }
    }
}

void Shell::activateNextTab()
{
    if (m_tabs.size() < 2) {
        return;
    }

    const int activeTab = m_tabWidget->currentIndex();
    const int nextTab = (activeTab == m_tabs.size() - 1) ? 0 : activeTab + 1;

    setActiveTab(nextTab);
}

void Shell::activatePrevTab()
{
    if (m_tabs.size() < 2) {
        return;
    }

    const int activeTab = m_tabWidget->currentIndex();
    const int prevTab = (activeTab == 0) ? m_tabs.size() - 1 : activeTab - 1;

    setActiveTab(prevTab);
}

void Shell::undoCloseTab()
{
    if (m_closedTabUrls.isEmpty()) {
        return;
    }

    const QUrl lastTabUrl = m_closedTabUrls.takeLast();

    if (m_closedTabUrls.isEmpty()) {
        m_undoCloseTab->setEnabled(false);
    }

    openUrl(lastTabUrl);
}

void Shell::setTabIcon(const QMimeType &mimeType)
{
    int i = findTabIndex(sender());
    if (i != -1) {
        m_tabWidget->setTabIcon(i, QIcon::fromTheme(mimeType.iconName()));
    }
}

int Shell::findTabIndex(QObject *sender) const
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].part == sender) {
            return i;
        }
    }
    return -1;
}

int Shell::findTabIndex(const QUrl &url) const
{
    auto it = std::find_if(m_tabs.begin(), m_tabs.end(), [&url](const TabState state) { return state.part->url() == url; });
    return (it != m_tabs.end()) ? std::distance(m_tabs.begin(), it) : -1;
}

void Shell::handleDroppedUrls(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        openUrl(url);
    }
}

void Shell::moveTabData(int from, int to)
{
    m_tabs.move(from, to);
    updateOpenTabButtonGeometry();
    scheduleOpenDocumentSessionSave();
}

void Shell::resetTabBarScrollToStart()
{
    if (!m_tabWidget || m_tabWidget->currentIndex() != 0) {
        return;
    }

    QTabBar *const tabBar = m_tabWidget->tabBar();
    const bool usesScrollButtons = tabBar->usesScrollButtons();
    tabBar->setUpdatesEnabled(false);
    tabBar->setUsesScrollButtons(false);
    tabBar->setUsesScrollButtons(usesScrollButtons);
    tabBar->setCurrentIndex(0);
    tabBar->setUpdatesEnabled(true);
    tabBar->updateGeometry();
    tabBar->update();
    updateOpenTabButtonGeometry();
}

void Shell::scrollTabBarToCurrentTab()
{
    if (!m_tabWidget || m_tabWidget->currentIndex() < 0) {
        return;
    }

    QTabBar *const tabBar = m_tabWidget->tabBar();
    const int currentIndex = m_tabWidget->currentIndex();
    tabBar->setCurrentIndex(currentIndex);

    QList<QToolButton *> scrollButtons = tabBar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    scrollButtons.erase(std::remove(scrollButtons.begin(), scrollButtons.end(), m_centerActiveTabButton), scrollButtons.end());
    scrollButtons.erase(std::remove_if(scrollButtons.begin(), scrollButtons.end(), [](const QToolButton *button) {
                            return !button || !button->isVisible() || button->geometry().isEmpty();
                        }),
                        scrollButtons.end());
    std::sort(scrollButtons.begin(), scrollButtons.end(), [](const QToolButton *a, const QToolButton *b) {
        return a->geometry().x() < b->geometry().x();
    });

    if (scrollButtons.size() >= 2) {
        QToolButton *const leftScrollButton = scrollButtons.at(scrollButtons.size() - 2);
        QToolButton *const rightScrollButton = scrollButtons.at(scrollButtons.size() - 1);
        const int visibleLeft = tabBar->rect().left();
        const int visibleRight = leftScrollButton->geometry().left() - 1;

        for (int i = 0; i < tabBar->count(); ++i) {
            const QRect currentRect = tabBar->tabRect(currentIndex);
            if (currentRect.left() >= visibleLeft && currentRect.right() <= visibleRight) {
                break;
            }

            if (currentRect.left() < visibleLeft) {
                leftScrollButton->click();
            } else {
                rightScrollButton->click();
            }
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }

    updateOpenTabButtonGeometry();
}

void Shell::scheduleOpenTabButtonGeometryUpdate()
{
    QTimer::singleShot(0, this, &Shell::updateOpenTabButtonGeometry);
    QTimer::singleShot(50, this, &Shell::updateOpenTabButtonGeometry);
    QTimer::singleShot(150, this, &Shell::updateOpenTabButtonGeometry);
}

void Shell::updateOpenTabButtonGeometry()
{
    if (!m_openTabButton || !m_centerActiveTabButton || !m_tabWidget) {
        return;
    }

    QTabBar *const tabBar = m_tabWidget->tabBar();
    if (tabBar->maximumWidth() != QWIDGETSIZE_MAX) {
        tabBar->setMaximumWidth(QWIDGETSIZE_MAX);
        m_tabWidget->updateGeometry();
        scheduleOpenTabButtonGeometryUpdate();
        return;
    }

    QList<QToolButton *> scrollButtons = tabBar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    scrollButtons.erase(std::remove(scrollButtons.begin(), scrollButtons.end(), m_centerActiveTabButton), scrollButtons.end());
    scrollButtons.erase(std::remove_if(scrollButtons.begin(), scrollButtons.end(), [](const QToolButton *button) {
                            return !button || !button->isVisible() || button->geometry().isEmpty();
                        }),
                        scrollButtons.end());
    std::sort(scrollButtons.begin(), scrollButtons.end(), [](const QToolButton *a, const QToolButton *b) {
        return a->geometry().x() < b->geometry().x();
    });

    if (scrollButtons.size() >= 2) {
        QToolButton *const leftScrollButton = scrollButtons.at(scrollButtons.size() - 2);
        QToolButton *const rightScrollButton = scrollButtons.at(scrollButtons.size() - 1);
        const QRect leftRect = leftScrollButton->geometry();
        const QRect rightRect = rightScrollButton->geometry();
        const int centerX = std::max(0, rightRect.x() - m_centerActiveTabButton->width());
        const int leftX = std::max(0, centerX - leftRect.width());

        leftScrollButton->setGeometry(leftX, rightRect.y(), leftRect.width(), rightRect.height());
        m_centerActiveTabButton->setGeometry(centerX, rightRect.y(), m_centerActiveTabButton->width(), rightRect.height());
        rightScrollButton->setGeometry(rightRect);
        m_centerActiveTabButton->show();
        leftScrollButton->raise();
        m_centerActiveTabButton->raise();
        rightScrollButton->raise();
    } else {
        m_centerActiveTabButton->hide();
    }

    m_openTabButton->show();
}

void Shell::slotFitWindowToPage(const QSize pageViewSize, const QSize pageSize)
{
    const int xOffset = pageViewSize.width() - pageSize.width();
    const int yOffset = pageViewSize.height() - pageSize.height();
    showNormal();
    resize(width() - xOffset, height() - yOffset);
    Q_EMIT moveSplitter(pageSize.width());
}

void Shell::hideWelcomeScreen()
{
    m_centralStackedWidget->setCurrentWidget(m_tabWidget);
    m_sidebar->setVisible(m_showSidebarAction->isChecked());
    m_showSidebarAction->setEnabled(true);
}

void Shell::showWelcomeScreen()
{
    m_showSidebarAction->setEnabled(false);
    m_centralStackedWidget->setCurrentWidget(m_welcomeScreen);
    m_sidebar->setVisible(false);

    refreshRecentsOnWelcomeScreen();
}

void Shell::refreshRecentsOnWelcomeScreen()
{
    saveRecents();
    m_welcomeScreen->loadRecents();
}

void Shell::forgetRecentItem(QUrl const &url)
{
    if (m_recent != nullptr) {
        m_recent->removeUrl(url);
        saveRecents();
        refreshRecentsOnWelcomeScreen();
    }
}

void Shell::triggerUpdateRecentItems(const int maxItems)
{
    m_recent->setMaxItems(maxItems);
    m_welcomeScreen->setMaxRecentItems(m_recent->maxItems());
    // saveRecents() dumps the recent files in correct order to KConfigGroup, respecting the allowed no. of recent items, discarding older items if needed
    refreshRecentsOnWelcomeScreen();
    m_recent->loadEntries(KSharedConfig::openConfig()->group(RecentFilesGroupKey()));
}

void Shell::readRecentFilesSettings()
{
    // Read no. of max. recent items from okularpartrc, populate File->Open Recent menu-item as well as recentsListView on welcome screen
    QString configFilePath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QLatin1Char('/') + QLatin1String("okularpartrc");
    const KConfigGroup confgrp = KSharedConfig::openConfig(configFilePath).data()->group(QStringLiteral("General"));
    const int defaultMaxRecentItems = 10;
    int maxRecentItems = confgrp.readEntry<int>("MaxRecentItems", defaultMaxRecentItems);
    m_recent->setMaxItems(maxRecentItems);
    m_welcomeScreen->setMaxRecentItems(m_recent->maxItems());
    m_welcomeScreen->loadRecents();
    m_recent->loadEntries(KSharedConfig::openConfig()->group(RecentFilesGroupKey()));
}

#include "moc_shell.cpp"
#include "shell.moc"
