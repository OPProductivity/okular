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
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#if HAVE_DBUS
#include <QDBusConnection>
#endif // HAVE_DBUS
#include <QDockWidget>
#include <QDragMoveEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QHash>
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
#include <QPainter>
#include <QPointer>
#include <QProxyStyle>
#include <QScreen>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QWindow>

// local includes
#include "../interfaces/viewerinterface.h"
#include "kdocumentviewer.h"
#include "okular_main.h"
#include "shellutils.h"

#include <algorithm>
#include <cmath>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <aclapi.h>
#include <sddl.h>
#endif

static const char *shouldShowMenuBarComingFromFullScreen = "shouldShowMenuBarComingFromFullScreen";
static const char *shouldShowToolBarComingFromFullScreen = "shouldShowToolBarComingFromFullScreen";

static const char *const SESSION_URL_KEY = "Urls";
static const char *const SESSION_TAB_KEY = "ActiveTab";
static const char *const SESSION_ACTIVE_URL_KEY = "ActiveUrl";
static constexpr int OPEN_DOCUMENT_SESSION_SAVE_DELAY_MS = 2000;
static constexpr qint64 CLOSED_WINDOW_RESTORE_GRACE_SECONDS = 15 * 60;
static constexpr qint64 RECENT_FILE_MAX_AGE_SECONDS = 3 * 24 * 60 * 60;
static constexpr int MAX_RECENT_FILES = 50;
#if defined(Q_OS_WIN)
static constexpr quint32 WINDOWS_TAB_OPEN_MAGIC = 0x4f4b5450; // OKTP
static constexpr quint32 WINDOWS_TAB_OPEN_ELEVATED_MAGIC = 0x4f4b5445; // OKTE
static constexpr quint32 WINDOWS_TAB_OPEN_MAX_REQUEST_BYTES = 4 * 1024 * 1024;
static constexpr int WINDOWS_BATCH_OPEN_SUMMARY_DELAY_MS = 1200;
static QPointer<QLocalServer> s_windowsTabOpenPrimaryServer;
static bool s_windowsSessionRestoreInProgress = false;

static QByteArray tokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS kind)
{
    DWORD length = 0;
    GetTokenInformation(token, kind, nullptr, 0, &length);
    if (!length) {
        return {};
    }
    QByteArray result(length, '\0');
    if (!GetTokenInformation(token, kind, result.data(), length, &length)) {
        return {};
    }
    return result;
}

static DWORD tokenIntegrityLevel(HANDLE token)
{
    const QByteArray data = tokenInformation(token, TokenIntegrityLevel);
    if (data.isEmpty()) {
        return 0;
    }
    const PSID sid = reinterpret_cast<const TOKEN_MANDATORY_LABEL *>(data.constData())->Label.Sid;
    if (!IsValidSid(sid)) {
        return 0;
    }
    const PUCHAR count = GetSidSubAuthorityCount(sid);
    return count && *count ? *GetSidSubAuthority(sid, *count - 1) : 0;
}

// Authenticate the token attached to the pipe, not the process ID reported by
// the client. In particular, a standard process must not command an elevated
// Okular through Qt's same-user named pipe ACL.
static bool trustedWindowsTabOpenClient(QLocalSocket *socket)
{
    const HANDLE pipe = reinterpret_cast<HANDLE>(socket->socketDescriptor());
    if (!pipe || pipe == INVALID_HANDLE_VALUE || !ImpersonateNamedPipeClient(pipe)) {
        return false;
    }
    HANDLE clientToken = nullptr;
    const bool openedClient = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &clientToken);
    if (!RevertToSelf()) {
        if (clientToken) {
            CloseHandle(clientToken);
        }
        qFatal("Could not restore Okular's security context after pipe authentication");
    }
    if (!openedClient) {
        return false;
    }
    HANDLE ownToken = nullptr;
    const bool openedOwn = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &ownToken);
    bool trusted = false;
    if (openedOwn) {
        const QByteArray client = tokenInformation(clientToken, TokenUser);
        const QByteArray own = tokenInformation(ownToken, TokenUser);
        const DWORD clientIntegrity = tokenIntegrityLevel(clientToken);
        const DWORD ownIntegrity = tokenIntegrityLevel(ownToken);
        trusted = !client.isEmpty() && !own.isEmpty() && clientIntegrity && ownIntegrity && clientIntegrity >= ownIntegrity
            && EqualSid(reinterpret_cast<const TOKEN_USER *>(client.constData())->User.Sid, reinterpret_cast<const TOKEN_USER *>(own.constData())->User.Sid);
        CloseHandle(ownToken);
    }
    CloseHandle(clientToken);
    return trusted;
}

// QWidget::setGeometry positions the client area. Account for the native title
// bar after show(), so an intentional window stays wholly visible.
static void placeIntentionalWindowFrame(QWidget *window, QRect frame, const QRect &available)
{
    QWindow *handle = window->windowHandle();
    if (!handle || available.isEmpty()) {
        return;
    }
    const QMargins margins = handle->frameMargins();
    frame.setSize(QSize(qMin(frame.width(), available.width()), qMin(frame.height(), available.height())));
    frame.moveLeft(qBound(available.left(), frame.left(), available.right() - frame.width() + 1));
    frame.moveTop(qBound(available.top(), frame.top(), available.bottom() - frame.height() + 1));
    window->setGeometry(frame.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom()));
}
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
static inline QString RecentOpenTimesGroupKey()
{
    return QStringLiteral("Recent File Open Times");
}
static QByteArray recentOpenTimeKey(const QUrl &url)
{
    return QCryptographicHash::hash(url.toString(QUrl::FullyEncoded).toUtf8(), QCryptographicHash::Sha256).toHex();
}
static inline QString GeneralGroupKey()
{
    return QStringLiteral("General");
}
static inline QString OpenDocumentSessionGroupKey()
{
#if defined(Q_OS_WIN)
    if (ShellUtils::windowsProcessIsElevated()) {
        return QStringLiteral("Shell Open Documents Session Elevated");
    }
#endif
    return QStringLiteral("Shell Open Documents Session");
}

static KSharedConfigPtr ShellSessionConfig()
{
#if defined(Q_OS_WIN)
    const QString testInstance = qEnvironmentVariable("OKULAR_TEST_INSTANCE");
    if (!testInstance.isEmpty()) {
        const QString testDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/test-sessions");
        QDir().mkpath(testDirectory);
        const QString instanceHash = QString::fromLatin1(QCryptographicHash::hash(testInstance.toUtf8(), QCryptographicHash::Sha256).toHex());
        const QString role = ShellUtils::windowsProcessIsElevated() ? QStringLiteral("elevated") : QStringLiteral("standard");
        return KSharedConfig::openConfig(testDirectory + QStringLiteral("/okular-%1-%2.ini").arg(role, instanceHash));
    }
    // Elevated shells must not race the normal shell's okularrc writes.
    if (ShellUtils::windowsProcessIsElevated()) {
        return KSharedConfig::openConfig(QStringLiteral("okular-elevated-sessionrc"));
    }
#endif
    return KSharedConfig::openConfig();
}

#if defined(Q_OS_WIN)
// Keep a small, private history of complete session configurations. These are
// local recovery copies, never project files or inputs to ordinary startup.
static bool secureSessionBackupDirectory(const QString &path)
{
    // Use the opened directory object for both validation and the ACL update.
    // A junction under the user's config tree must not redirect an elevated
    // Okular into changing the permissions of an unrelated directory.
    const std::wstring directory = QDir::toNativeSeparators(path).toStdWString();
    HANDLE directoryHandle = CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL | WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directoryHandle == INVALID_HANDLE_VALUE) {
        return false;
    }
    BY_HANDLE_FILE_INFORMATION fileInfo = {};
    const DWORD pathLength = GetFinalPathNameByHandleW(directoryHandle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    std::wstring resolved(pathLength, L'\0');
    const DWORD resolvedLength = pathLength ? GetFinalPathNameByHandleW(directoryHandle, resolved.data(), pathLength, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS) : 0;
    const QString resolvedPath = QString::fromWCharArray(resolved.data(), resolvedLength);
    const QString expectedPath = QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(QDir::cleanPath(path));
    if (!GetFileInformationByHandle(directoryHandle, &fileInfo) || (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !resolvedLength || resolvedLength >= pathLength
        || resolvedPath.compare(expectedPath, Qt::CaseInsensitive) != 0) {
        CloseHandle(directoryHandle);
        return false;
    }
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        CloseHandle(directoryHandle);
        return false;
    }
    DWORD length = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &length);
    QByteArray user(length, '\0');
    const bool hasUser = length && GetTokenInformation(token, TokenUser, user.data(), length, &length);
    CloseHandle(token);
    if (!hasUser) {
        CloseHandle(directoryHandle);
        return false;
    }

    LPWSTR userSid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid, &userSid)) {
        CloseHandle(directoryHandle);
        return false;
    }
    const QString descriptor = QStringLiteral("D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;%1)").arg(QString::fromWCharArray(userSid));
    LocalFree(userSid);
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    const std::wstring descriptorString = descriptor.toStdWString();
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptorString.c_str(), SDDL_REVISION_1, &securityDescriptor, nullptr)) {
        CloseHandle(directoryHandle);
        return false;
    }
    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    PACL dacl = nullptr;
    const bool hasDacl = GetSecurityDescriptorDacl(securityDescriptor, &present, &dacl, &defaulted) && present && dacl;
    const bool secured = hasDacl && SetSecurityInfo(directoryHandle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl, nullptr) == ERROR_SUCCESS;
    LocalFree(securityDescriptor);
    CloseHandle(directoryHandle);
    return secured;
}

static void saveSessionRecoveryCopy(const KSharedConfigPtr &config)
{
    QString sourcePath = config->name();
    if (QFileInfo(sourcePath).isRelative()) {
        sourcePath = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(sourcePath);
    }
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not read Okular session configuration for recovery copy";
        return;
    }
    const QByteArray contents = source.readAll();
    if (contents.isEmpty()) {
        return;
    }

    const QString testInstance = qEnvironmentVariable("OKULAR_TEST_INSTANCE");
    const QString directoryPath = QFileInfo(sourcePath).absolutePath() + (testInstance.isEmpty() ? QStringLiteral("/okular/session-backups") : QStringLiteral("/session-backups"));
    if (!QDir().mkpath(directoryPath) || !secureSessionBackupDirectory(directoryPath)) {
        qWarning() << "Could not secure Okular session recovery directory";
        return;
    }
    QDir directory(directoryPath);
    const QString role = ShellUtils::windowsProcessIsElevated() ? QStringLiteral("elevated") : QStringLiteral("standard");
    const QString prefix = QStringLiteral("okular-session-%1-").arg(role);
    const QStringList existing = directory.entryList({prefix + QStringLiteral("*.ini")}, QDir::Files, QDir::Name);
    if (!existing.isEmpty()) {
        QFile latest(directory.filePath(existing.constLast()));
        if (latest.open(QIODevice::ReadOnly) && latest.readAll() == contents) {
            return;
        }
    }

    const QString name = prefix + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ")) + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".ini");
    QSaveFile backup(directory.filePath(name));
    if (!backup.open(QIODevice::WriteOnly) || backup.write(contents) != contents.size() || !backup.commit()) {
        qWarning() << "Could not save Okular session recovery copy";
        return;
    }
    const QStringList retained = directory.entryList({prefix + QStringLiteral("*.ini")}, QDir::Files, QDir::Name);
    for (int i = 0; i < retained.size() - 10; ++i) {
        directory.remove(retained.at(i));
    }
}

static quint64 s_nextSessionWindowOrder = 0;

static QString WindowSessionIndexGroupKey()
{
    return OpenDocumentSessionGroupKey() + QStringLiteral(" Windows");
}

static QString WindowSessionGroupKey(const QString &id)
{
    return OpenDocumentSessionGroupKey() + QStringLiteral(" Window ") + id;
}

struct SavedWindowSession {
    QStringList urls;
    int activeTab = 0;
    QString activeUrl;
    bool intentionalSeparateWindow = false;
    QByteArray geometry;
};

static QHash<QString, SavedWindowSession> s_startupWindowSessions;
static QStringList s_startupWindowIds;
static QSet<QString> s_failedWindowRestores;
static bool s_startupWindowSnapshotCaptured = false;

static SavedWindowSession readWindowSession(const KConfigGroup &group)
{
    SavedWindowSession session;
    session.urls = group.readPathEntry(SESSION_URL_KEY, QStringList());
    session.activeTab = group.readEntry<int>(SESSION_TAB_KEY, 0);
    session.activeUrl = group.readPathEntry(SESSION_ACTIVE_URL_KEY, QString());
    session.intentionalSeparateWindow = group.readEntry("IntentionalSeparateWindow", false);
    session.geometry = group.readEntry("WindowGeometry", QByteArray());
    return session;
}

static QStringList savedWindowIds(const KSharedConfigPtr &config)
{
    const QStringList stored = config->group(WindowSessionIndexGroupKey()).readEntry("WindowIds", QStringList());
    QStringList valid;
    for (const QString &id : stored) {
        const QUuid uuid(id);
        if (!uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id && !valid.contains(id)) {
            valid.append(id);
        }
    }
    return valid;
}

static bool wasWindowClosedRecently(qint64 closedAt, qint64 now)
{
    if (closedAt <= 0) {
        return false;
    }
    // The system clock can be corrected backwards between two window closes.
    return closedAt > now ? closedAt - now <= CLOSED_WINDOW_RESTORE_GRACE_SECONDS : now - closedAt <= CLOSED_WINDOW_RESTORE_GRACE_SECONDS;
}
#endif

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

class ThreeButtonTabBarStyle : public QProxyStyle
{
public:
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override
    {
        const auto *bar = qobject_cast<const QTabBar *>(widget);
        if (option && bar && baseStyle()->name() == QLatin1String("windows11") && (element == CE_TabBarTabLabel || (element == CE_TabBarTabShape && (option->state & State_Selected)))) {
            // The Windows 11 proxy loses native dark-theme label colors and the
            // selected tab's accent edge. Keep native painting for both.
            QApplication::style()->drawControl(element, option, painter, widget);
        } else {
            QProxyStyle::drawControl(element, option, painter, widget);
        }
        if (bar && option && element == CE_TabBarTabShape) {
            const int tab = bar->tabAt(option->rect.center());
            if (tab >= 0 && bar->tabData(tab).toBool() && !(option->state & State_Selected)) {
                painter->save();
                painter->setPen(QPen(option->palette.highlight().color(), 2));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(option->rect.adjusted(1, 1, -2, -2));
                painter->restore();
            }
        }
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        if (metric == PM_TabBarScrollButtonWidth && qobject_cast<const QTabBar *>(widget)) {
            // Qt allocates two scroller widths for the three 20-pixel controls.
            return 30;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    QRect subElementRect(SubElement element, const QStyleOption *option, const QWidget *widget) const override
    {
        if ((element == SE_TabBarScrollLeftButton || element == SE_TabBarScrollRightButton) && qobject_cast<const QTabBar *>(widget)) {
            const QRect nativeRightRect = QProxyStyle::subElementRect(SE_TabBarScrollRightButton, option, widget);
            const int buttonWidth = pixelMetric(PM_TabBarScrollButtonWidth, option, widget);
            const int x = widget->layoutDirection() == Qt::RightToLeft
                ? nativeRightRect.left() + (element == SE_TabBarScrollLeftButton ? buttonWidth : 0)
                : nativeRightRect.right() + 1 - (element == SE_TabBarScrollLeftButton ? 2 : 1) * buttonWidth;
            return QRect(x, nativeRightRect.y(), buttonWidth, nativeRightRect.height());
        }
        return QProxyStyle::subElementRect(element, option, widget);
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

Shell::Shell(const QString &serializedOptions, const QString &sessionWindowId, bool intentionalSeparateWindow)
    : KParts::MainWindow()
    , m_menuBarWasShown(true)
    , m_toolBarWasShown(true)
    , m_isValid(true)
{
#if defined(Q_OS_WIN)
    const QUuid savedId(sessionWindowId);
    m_sessionWindowId = savedId.isNull() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : savedId.toString(QUuid::WithoutBraces);
    m_sessionWindowOrder = ++s_nextSessionWindowOrder;
    m_intentionalSeparateWindow = intentionalSeparateWindow;
#else
    Q_UNUSED(sessionWindowId);
    Q_UNUSED(intentionalSeparateWindow);
#endif
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
        m_centralStackedWidget->setAutoFillBackground(true);
        setCentralWidget(m_centralStackedWidget);

        // Setup the welcome screen
        m_welcomeScreen = new WelcomeScreen(this);
        connect(m_welcomeScreen, &WelcomeScreen::openClicked, this, &Shell::fileOpen);
        connect(m_welcomeScreen, &WelcomeScreen::closeClicked, this, &Shell::hideWelcomeScreen);
        connect(m_welcomeScreen, &WelcomeScreen::recentItemClicked, this, [this](const QUrl &url) { openUrl(url); });
        connect(m_welcomeScreen, &WelcomeScreen::openRecentDocuments, this, &Shell::openRecentDocuments);
        connect(m_welcomeScreen, &WelcomeScreen::forgetRecentItems, this, &Shell::forgetRecentItems);
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
        auto *const tabBarStyle = new ThreeButtonTabBarStyle;
        tabBarStyle->setParent(m_tabWidget->tabBar());
        m_tabWidget->tabBar()->setStyle(tabBarStyle);
        m_tabWidget->setDocumentMode(true);
        m_tabWidget->setMovable(true);

        QWidget *const tabCornerWidget = new QWidget(m_tabWidget);
        tabCornerWidget->setObjectName(QStringLiteral("tabCornerWidget"));
        QHBoxLayout *const tabCornerLayout = new QHBoxLayout(tabCornerWidget);
        constexpr int openButtonGap = 3;
        tabCornerLayout->setContentsMargins(openButtonGap, 0, 1, 0);
        tabCornerLayout->setSpacing(1);

        const QString tabCornerButtonStyle = QStringLiteral("QToolButton { border: 1px solid transparent; border-radius: 2px; padding: 0px 0px 2px 0px; }"
                                                            "QToolButton:hover, QToolButton:focus { color: #3daee9; border: 1px solid #3daee9; }"
                                                            "QToolButton:pressed { color: #3daee9; border: 1px solid #3daee9; background-color: rgba(61, 174, 233, 35); }");

        m_centerActiveTabButton = new QToolButton(m_tabWidget->tabBar());
        m_centerActiveTabButton->setText(QStringLiteral("o"));
        m_centerActiveTabButton->setToolTip(i18n("Center Active Tab"));
        m_centerActiveTabButton->setAccessibleName(i18n("Center Active Tab"));
        QFont centerActiveTabButtonFont = m_centerActiveTabButton->font();
        centerActiveTabButtonFont.setPixelSize(13);
        centerActiveTabButtonFont.setBold(false);
        m_centerActiveTabButton->setFont(centerActiveTabButtonFont);
        m_centerActiveTabButton->setFixedWidth(20);
        m_centerActiveTabButton->setAutoRaise(false);
        m_centerActiveTabButton->hide();
        connect(m_centerActiveTabButton, &QToolButton::clicked, this, &Shell::centerActiveTabInTabBar);

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
        tabCornerWidget->setFixedWidth(m_openTabButton->width() + openButtonGap + 1);
        m_tabWidget->setCornerWidget(tabCornerWidget, Qt::TopRightCorner);

        m_tabWidget->setAcceptDrops(true);
        m_centralStackedWidget->installEventFilter(this);
        m_tabWidget->installEventFilter(this);
        m_tabWidget->tabBar()->installEventFilter(this);

        m_centralStackedWidget->addWidget(m_tabWidget);

        connect(m_tabWidget, &QTabWidget::currentChanged, this, &Shell::setActiveTab);
#if defined(Q_OS_WIN)
        connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &Shell::closeRequestedTabs);
#else
        connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &Shell::closeTab);
#endif
        connect(m_tabWidget->tabBar(), &QTabBar::tabMoved, this, &Shell::moveTabData);

        m_openDocumentSessionSaveTimer = new QTimer(this);
        m_openDocumentSessionSaveTimer->setSingleShot(true);
        connect(m_openDocumentSessionSaveTimer, &QTimer::timeout, this, &Shell::saveOpenDocumentSession);
#if defined(Q_OS_WIN)
        m_windowsBatchOpenSummaryTimer = new QTimer(this);
        m_windowsBatchOpenSummaryTimer->setSingleShot(true);
        connect(m_windowsBatchOpenSummaryTimer, &QTimer::timeout, this, &Shell::showWindowsBatchOpenSummary);
#endif

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
#if defined(Q_OS_WIN)
    if (m_tabWidget && obj == m_tabWidget->tabBar()) {
        QTabBar *bar = m_tabWidget->tabBar();
        if (event->type() == QEvent::ContextMenu) {
            auto *context = static_cast<QContextMenuEvent *>(event);
            const int tab = bar->tabAt(context->pos());
            if (tab >= 0 && !m_tabs.at(tab).part->url().isEmpty()) {
                if (!bar->tabData(tab).toBool()) {
                    selectTabsForClick(tab, Qt::NoModifier);
                }
                QMenu menu(bar);
                menu.setMaximumWidth(450);
                QAction *move = menu.addAction(i18n("Move to New Window"));
                QHash<QAction *, QPointer<Shell>> otherWindows;
                const auto containsPdf = [](const Shell *window) {
                    return std::any_of(window->m_tabs.cbegin(), window->m_tabs.cend(), [](const TabState &state) {
                        return state.part->url().path().endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive);
                    });
                };
                if (containsPdf(this)) {
                    QHash<QString, int> labelCounts;
                    for (QWidget *widget : QApplication::topLevelWidgets()) {
                        Shell *other = qobject_cast<Shell *>(widget);
                        if (!other || other == this || !other->isVisible() || other->m_sessionClosed || other->m_unique || !containsPdf(other)) {
                            continue;
                        }
                        const int active = other->m_tabWidget->currentIndex();
                        const QString title = other->m_tabWidget->tabText(active);
                        const int instance = ++labelCounts[title];
                        const QString suffix = instance > 1 ? QStringLiteral(" (%1)").arg(instance) : QString();
                        // Reserve room for the menu margins, icon column and a duplicate suffix.
                        const QString frame = i18n("Move to Window: “%1”", QString());
                        const int titleWidth = qMax(40, 450 - 50 - menu.fontMetrics().horizontalAdvance(frame + suffix));
                        const QString displayTitle = menu.fontMetrics().elidedText(title, Qt::ElideRight, titleWidth);
                        QAction *action = menu.addAction(i18n("Move to Window: “%1”", displayTitle) + suffix);
                        action->setToolTip(other->m_tabWidget->tabToolTip(active));
                        otherWindows.insert(action, other);
                    }
                }
                QAction *chosen = menu.exec(context->globalPos());
                if (chosen == move) {
                    moveTabsToWindow(selectedTabsFor(tab), nullptr, context->globalPos());
                } else if (otherWindows.contains(chosen) && otherWindows.value(chosen)) {
                    moveTabsToWindow(selectedTabsFor(tab), otherWindows.value(chosen));
                }
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const int tab = bar->tabAt(mouse->pos());
            if (mouse->button() == Qt::LeftButton && tab >= 0) {
                selectTabsForClick(tab, mouse->modifiers());
            }
            m_draggedTabPart = mouse->button() == Qt::LeftButton && tab >= 0 && mouse->modifiers() == Qt::NoModifier ? m_tabs.at(tab).part : nullptr;
            m_tabDragStart = mouse->globalPosition().toPoint();
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            QPointer<KParts::ReadWritePart> dragged = m_draggedTabPart;
            m_draggedTabPart = nullptr;
            const QPoint drop = mouse->globalPosition().toPoint();
            if (mouse->button() == Qt::LeftButton && dragged && !frameGeometry().contains(drop) && (drop - m_tabDragStart).manhattanLength() >= QApplication::startDragDistance()) {
                // Let QTabBar finish its own mouse release before moving the page.
                QTimer::singleShot(0, this, [this, dragged, drop]() {
                    if (dragged) {
                        moveTabToNewWindow(findTabIndex(dragged), drop);
                    }
                });
            }
        }
    }
#endif
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
                closeRequestedTabs(tabIndex);
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

#if !defined(Q_OS_WIN)
    hideWelcomeScreen();
#endif

#if !defined(Q_OS_WIN)
    KParts::ReadWritePart *const part = m_tabs[0].part;
    if (!qobject_cast<Okular::ViewerInterface *>(part)->openNewFilesInTabs() && !part->url().isEmpty() && !ShellUtils::unique(serializedOptions)) {
        return false;
    }
#endif

    openUrl(url, serializedOptions);

    return true;
}

bool Shell::openDocumentInTab(const QUrl &url, const QString &serializedOptions)
{
    if (m_tabs.isEmpty()) {
        return false;
    }

    if (m_tabs[m_tabWidget->currentIndex()].part->url().isEmpty()) {
        openUrl(url, serializedOptions);
    } else {
        openNewTab(url, serializedOptions);
    }
    return true;
}

void Shell::prepareForDocumentOpen()
{
    hideWelcomeScreen();
}

bool Shell::openDocument(const QString &urlString, const QString &serializedOptions)
{
    return openDocument(QUrl(urlString), serializedOptions);
}

#if defined(Q_OS_WIN)
void Shell::startWindowsTabOpenServer()
{
    if (m_unique || m_windowsTabOpenServer || s_windowsTabOpenPrimaryServer || !Okular::claimWindowsTabOpenPrimary()) {
        return;
    }

    m_windowsTabOpenServer = new QLocalServer(this);
    m_windowsTabOpenServer->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_windowsTabOpenServer, &QLocalServer::newConnection, this, &Shell::handleWindowsTabOpenConnection);

    if (m_windowsTabOpenServer->listen(ShellUtils::windowsTabOpenServerName())) {
        s_windowsTabOpenPrimaryServer = m_windowsTabOpenServer;
        return;
    }

    if (m_windowsTabOpenServer->serverError() == QAbstractSocket::AddressInUseError) {
        QLocalSocket probeSocket;
        probeSocket.connectToServer(ShellUtils::windowsTabOpenServerName(), QIODevice::WriteOnly);
        if (probeSocket.waitForConnected(250)) {
            // Another private Okular shell already owns the forwarding endpoint.
            // Do not steal it; otherwise future file opens can target the wrong window.
            probeSocket.disconnectFromServer();
            m_windowsTabOpenServer->deleteLater();
            m_windowsTabOpenServer = nullptr;
            return;
        }

        QLocalServer::removeServer(ShellUtils::windowsTabOpenServerName());
        if (m_windowsTabOpenServer->listen(ShellUtils::windowsTabOpenServerName())) {
            s_windowsTabOpenPrimaryServer = m_windowsTabOpenServer;
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
        const QPointer<QLocalSocket> socketGuard(socket);
        socket->setParent(this);
        socket->setReadBufferSize(WINDOWS_TAB_OPEN_MAX_REQUEST_BYTES + sizeof(quint32));
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(10000, socket, [socket]() { socket->disconnectFromServer(); });

        const auto readRequest = [this, socketGuard]() {
            QLocalSocket *socket = socketGuard.data();
            if (!socket) {
                return;
            }
            if (!trustedWindowsTabOpenClient(socket)) {
                socket->disconnectFromServer();
                return;
            }
            if (socket->bytesAvailable() < qint64(sizeof(quint32))) {
                return;
            }
            QByteArray header = socket->peek(sizeof(quint32));
            QDataStream headerStream(&header, QIODevice::ReadOnly);
            headerStream.setVersion(QDataStream::Qt_6_0);
            quint32 length = 0;
            headerStream >> length;
            if (length > WINDOWS_TAB_OPEN_MAX_REQUEST_BYTES || length < sizeof(quint32)) {
                socket->disconnectFromServer();
                return;
            }
            if (socket->bytesAvailable() < qint64(sizeof(quint32)) + length) {
                return;
            }
            socket->read(sizeof(quint32));
            QByteArray payload = socket->read(length);
            QDataStream stream(&payload, QIODevice::ReadOnly);
            stream.setVersion(QDataStream::Qt_6_0);

            quint32 magic = 0;
            QString serializedOptions;
            QStringList paths;
            stream >> magic >> serializedOptions >> paths;

            if (stream.status() != QDataStream::Ok || !stream.atEnd() || paths.size() > 1024 || serializedOptions.size() > 4096) {
                socket->disconnectFromServer();
                return;
            }

            if (magic == WINDOWS_TAB_OPEN_MAGIC || magic == WINDOWS_TAB_OPEN_ELEVATED_MAGIC) {
                if (magic == WINDOWS_TAB_OPEN_ELEVATED_MAGIC) {
                    for (const QString &path : std::as_const(paths)) {
                        const QUrl url = ShellUtils::urlFromArg(path, ShellUtils::qfileExistFunc());
                        if (url.isLocalFile()) {
                            QFile file(url.toLocalFile());
                            if (!file.open(QIODevice::ReadOnly)) {
                                socket->write("D", 1);
                                socket->flush();
                                socket->disconnectFromServer();
                                socket->deleteLater();
                                return;
                            }
                        }
                    }
                }
                // Confirm receipt before document loading can block the GUI thread.
                socket->write("A", 1);
                socket->flush();
                const QString page = ShellUtils::page(serializedOptions);
                Shell *raiseTarget = this;
                bool countedRequest = false;
                for (const QString &path : std::as_const(paths)) {
                    if (path == QLatin1String("-")) {
                        continue;
                    }
                    const QUrl url = ShellUtils::urlFromArg(path, ShellUtils::qfileExistFunc(), page);
                    const bool alreadyOpen = findTabIndex(url) >= 0 || otherWindowWithUrl(url);
                    openDocument(url, serializedOptions);
                    if (findTabIndex(url) >= 0) {
                        raiseTarget = this;
                    } else if (Shell *other = otherWindowWithUrl(url)) {
                        raiseTarget = other;
                    }
                    if (alreadyOpen) {
                        ++m_windowsBatchAlreadyOpenCount;
                        countedRequest = true;
                    } else if (findTabIndex(url) >= 0 || otherWindowWithUrl(url)) {
                        ++m_windowsBatchOpenedCount;
                        countedRequest = true;
                    }
                }
                if (countedRequest) {
                    m_windowsBatchSummaryTarget = raiseTarget;
                    m_windowsBatchOpenSummaryTimer->start(WINDOWS_BATCH_OPEN_SUMMARY_DELAY_MS);
                }

                if (!ShellUtils::noRaise(serializedOptions)) {
                    raiseTarget->raisePrivateWindowsShell();
                    QTimer::singleShot(150, raiseTarget, &Shell::raisePrivateWindowsShell);
                }
            }

            // The acknowledged launcher can disconnect while a document error
            // dialog runs a nested event loop, deleting this socket mid-open.
            if (socketGuard) {
                socketGuard->disconnectFromServer();
                socketGuard->deleteLater();
            }
        };

        connect(socket, &QLocalSocket::readyRead, this, readRequest);
        if (socket->bytesAvailable() > 0) {
            readRequest();
        }
    }
}

void Shell::showWindowsBatchOpenSummary()
{
    if (m_windowsBatchAlreadyOpenCount > 0) {
        const QString message = i18np("%1 document opened", "%1 documents opened", m_windowsBatchOpenedCount)
            + i18np("; %1 was already open", "; %1 were already open", m_windowsBatchAlreadyOpenCount);
        Shell *target = m_windowsBatchSummaryTarget && m_windowsBatchSummaryTarget->isVisible() ? m_windowsBatchSummaryTarget.data() : this;
        target->statusBar()->showMessage(message, 6000);
    }
    m_windowsBatchOpenedCount = 0;
    m_windowsBatchAlreadyOpenCount = 0;
    m_windowsBatchSummaryTarget.clear();
}

void Shell::raisePrivateWindowsShell()
{
    if (!m_intentionalSeparateWindow && !isFullScreen() && (isMinimized() || isMaximized())) {
        showNormal();
    } else {
        show();
    }

    // Every regular Windows file launch starts from the same normal-window
    // placement, including launches forwarded to an existing shell.
    if (!m_intentionalSeparateWindow) {
        applyPrivateWindowsStartupGeometry();
    }

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
    const int activeTab = m_tabWidget->currentIndex();
    KParts::ReadWritePart *const activePart = m_tabs[activeTab].part;
#if defined(Q_OS_WIN)
    if (!m_unique) {
        const int existingTab = findTabIndex(url);
        if (existingTab >= 0) {
            setActiveTab(existingTab);
            addRecentUrl(url);
            return;
        }
        // A detached document still belongs to this process. Ordinary opens
        // should focus that tab, while an intentional separate window may
        // display another view of the same document.
        if (!m_intentionalSeparateWindow && !m_restoringOpenDocumentSession) {
            if (Shell *other = otherWindowWithUrl(url)) {
                other->setActiveTab(other->findTabIndex(url));
                other->raisePrivateWindowsShell();
                other->addRecentUrl(url);
                return;
            }
        }
    }
#endif
    hideWelcomeScreen();
    if (!activePart->url().isEmpty()) {
        if (m_unique) {
            applyOptionsToPart(activePart, serializedOptions);
            if (activePart->openUrl(url)) {
                addRecentUrl(url);
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
                addRecentUrl(url);
                scheduleOpenDocumentSessionSave();
            } else {
                m_recent->removeUrl(url);
                saveRecents();
                closeTab(activeTab);
            }
        }
    }
}

void Shell::openRecentDocuments(const QList<QUrl> &urls)
{
    int mostRecentTab = -1;
    for (const QUrl &url : urls) {
        if (!url.isValid() || url.isEmpty() || (url.isLocalFile() && !QFile::exists(url.toLocalFile()))) {
            continue;
        }
        int tab = findTabIndex(url);
        const bool alreadyOpen = tab >= 0;
        if (tab < 0) {
#if defined(Q_OS_WIN)
            if (Shell *other = otherWindowWithUrl(url)) {
                other->setActiveTab(other->findTabIndex(url));
                other->addRecentUrl(url);
                continue;
            }
#endif
            if (m_tabs[m_tabWidget->currentIndex()].part->url().isEmpty()) {
                openUrl(url);
            } else {
                openNewTab(url, QString());
            }
            tab = findTabIndex(url);
        }
        if (tab >= 0) {
            if (alreadyOpen) {
                addRecentUrl(url);
            }
            mostRecentTab = tab;
        }
    }
    if (mostRecentTab >= 0) {
        setActiveTab(mostRecentTab);
    }
}

void Shell::closeUrl()
{
#if defined(Q_OS_WIN)
    int requestedTab = m_tabWidget->currentIndex();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (m_tabWidget->tabBar()->tabData(i).toBool()) {
            requestedTab = i;
            break;
        }
    }
    closeRequestedTabs(requestedTab);
#else
    closeTab(m_tabWidget->currentIndex());
#endif

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

#if defined(Q_OS_WIN)
QList<int> Shell::selectedTabsFor(int tab) const
{
    QList<int> selected;
    if (tab < 0 || tab >= m_tabs.size()) {
        return selected;
    }
    if (m_tabWidget->tabBar()->tabData(tab).toBool()) {
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (m_tabWidget->tabBar()->tabData(i).toBool()) {
                selected.append(i);
            }
        }
    } else {
        selected.append(tab);
    }
    return selected;
}

void Shell::selectTabsForClick(int tab, Qt::KeyboardModifiers modifiers)
{
    if (tab < 0 || tab >= m_tabs.size()) {
        return;
    }
    QTabBar *bar = m_tabWidget->tabBar();
    int anchor = findTabIndex(m_tabSelectionAnchor);
    if (modifiers & Qt::ShiftModifier) {
        if (anchor < 0) {
            anchor = m_tabWidget->currentIndex();
        }
        if (anchor < 0) {
            anchor = tab;
        }
        if (!(modifiers & Qt::ControlModifier)) {
            for (int i = 0; i < bar->count(); ++i) {
                bar->setTabData(i, false);
            }
        }
        for (int i = qMin(anchor, tab); i <= qMax(anchor, tab); ++i) {
            bar->setTabData(i, true);
        }
        m_tabSelectionAnchor = m_tabs.at(anchor).part;
    } else if (modifiers & Qt::ControlModifier) {
        bar->setTabData(tab, !bar->tabData(tab).toBool());
        m_tabSelectionAnchor = m_tabs.at(tab).part;
    } else {
        for (int i = 0; i < bar->count(); ++i) {
            bar->setTabData(i, i == tab);
        }
        m_tabSelectionAnchor = m_tabs.at(tab).part;
    }
    bar->update();
}

void Shell::closeRequestedTabs(int tab)
{
    const QList<int> selected = selectedTabsFor(tab);
    for (auto it = selected.crbegin(); it != selected.crend(); ++it) {
        const bool closed = closeTab(*it);
        // A modified document can veto closeUrl(). Leave the remaining tabs alone.
        if (!closed) {
            break;
        }
    }
    m_tabSelectionAnchor = nullptr;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        m_tabWidget->tabBar()->setTabData(i, false);
    }
    m_tabWidget->tabBar()->update();
}
#endif

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
#if defined(Q_OS_WIN)
    if (ShellUtils::windowsProcessIsElevated()) {
        ShellSessionConfig()->sync();
        return;
    }
#endif

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
    pruneRecentFiles();
    KSharedConfigPtr config = ShellSessionConfig();
    m_recent->saveEntries(config->group(RecentFilesGroupKey()));
#if defined(Q_OS_WIN)
    // Every Shell owns a recent-file action, but the process shares one list.
    // A later save from another window must not overwrite this change.
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        Shell *other = qobject_cast<Shell *>(widget);
        if (other && other != this && other->m_recent) {
            other->m_recent->loadEntries(config->group(RecentFilesGroupKey()));
            if (other->m_centralStackedWidget->currentWidget() == other->m_welcomeScreen) {
                other->m_welcomeScreen->loadRecents();
            }
        }
    }
#endif
}

void Shell::addRecentUrl(const QUrl &url)
{
    m_recent->addUrl(url);
    if (m_recent->urls().contains(url)) {
        ShellSessionConfig()->group(RecentOpenTimesGroupKey()).writeEntry(recentOpenTimeKey(url).constData(), QDateTime::currentSecsSinceEpoch());
        saveRecents();
    }
}

void Shell::pruneRecentFiles()
{
    KConfigGroup times = ShellSessionConfig()->group(RecentOpenTimesGroupKey());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSet<QByteArray> retainedKeys;
    for (const QUrl &url : m_recent->urls()) {
        const QByteArray key = recentOpenTimeKey(url);
        const qint64 openedAt = times.readEntry(key.constData(), qint64(0));
        if (openedAt <= 0 || openedAt > now || now - openedAt > RECENT_FILE_MAX_AGE_SECONDS) {
            m_recent->removeUrl(url);
        } else {
            retainedKeys.insert(key);
        }
    }
    for (const QString &key : times.keyList()) {
        if (!retainedKeys.contains(key.toLatin1())) {
            times.deleteEntry(key);
        }
    }
}

void Shell::setupActions()
{
    KStandardAction::open(this, SLOT(fileOpen()), actionCollection());
#if defined(Q_OS_WIN)
    QAction *openNewWindow = actionCollection()->addAction(QStringLiteral("file_open_new_window"));
    openNewWindow->setText(i18n("Open in New Window..."));
    openNewWindow->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
    connect(openNewWindow, &QAction::triggered, this, &Shell::fileOpenInNewWindow);
#endif
    m_recent = KStandardAction::openRecent(this, SLOT(openUrl(QUrl)), actionCollection());
    m_recent->setToolBarMode(KRecentFilesAction::MenuMode);
    connect(m_recent, &QAction::triggered, this, &Shell::showOpenRecentMenu);
    connect(m_recent->menu(), &QMenu::aboutToShow, this, &Shell::saveRecents);
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
    const QString configFilePath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QLatin1String("/okularpartrc");
    return KSharedConfig::openConfig(configFilePath)->group(GeneralGroupKey()).readEntry("ShellRestoreOpenDocuments", true);
}

#if defined(Q_OS_WIN)
void Shell::setWindowsSessionRestoreInProgress(bool inProgress)
{
    s_windowsSessionRestoreInProgress = inProgress;
    s_startupWindowSessions.clear();
    s_startupWindowIds.clear();
    s_startupWindowSnapshotCaptured = false;
    if (inProgress) {
        s_failedWindowRestores.clear();
    }
}

QStringList Shell::savedOpenDocumentSessionWindowIds()
{
    if (s_windowsSessionRestoreInProgress && s_startupWindowSnapshotCaptured) {
        return s_startupWindowIds;
    }
    const QString configFilePath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QLatin1String("/okularpartrc");
    if (!KSharedConfig::openConfig(configFilePath)->group(GeneralGroupKey()).readEntry("ShellRestoreOpenDocuments", true)) {
        return {};
    }
    const KSharedConfigPtr config = ShellSessionConfig();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QStringList restorable;
    const bool capture = s_windowsSessionRestoreInProgress && !s_startupWindowSnapshotCaptured;
    for (const QString &id : savedWindowIds(config)) {
        const KConfigGroup group = config->group(WindowSessionGroupKey(id));
        const qint64 closedAt = group.readEntry("ClosedAt", qint64(0));
        if (closedAt == 0 || wasWindowClosedRecently(closedAt, now)) {
            restorable.append(id);
            if (capture) {
                s_startupWindowSessions.insert(id, readWindowSession(group));
            }
        }
    }
    if (capture) {
        s_startupWindowIds = restorable;
        s_startupWindowSnapshotCaptured = true;
    }
    return restorable;
}

void Shell::retainFailedWindowSession(const QString &sessionWindowId)
{
    if (!sessionWindowId.isEmpty()) {
        s_failedWindowRestores.insert(sessionWindowId);
    }
}

bool Shell::savedWindowWasSeparate(const QString &sessionWindowId)
{
    if (sessionWindowId.isEmpty()) {
        return false;
    }
    if (const auto saved = s_startupWindowSessions.constFind(sessionWindowId); saved != s_startupWindowSessions.cend()) {
        return saved->intentionalSeparateWindow;
    }
    return ShellSessionConfig()->group(WindowSessionGroupKey(sessionWindowId)).readEntry("IntentionalSeparateWindow", false);
}

void Shell::restoreSavedWindowGeometry()
{
    if (!m_intentionalSeparateWindow) {
        return;
    }
    const auto saved = s_startupWindowSessions.constFind(m_sessionWindowId);
    const QByteArray geometry = saved != s_startupWindowSessions.cend() ? saved->geometry : ShellSessionConfig()->group(WindowSessionGroupKey(m_sessionWindowId)).readEntry("WindowGeometry", QByteArray());
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
        QTimer::singleShot(0, this, [this]() {
            if (!isVisible() || isMaximized() || isFullScreen()) {
                return;
            }
            QScreen *target = QGuiApplication::screenAt(frameGeometry().center());
            if (!target) {
                target = screen() ? screen() : QGuiApplication::primaryScreen();
            }
            if (target) {
                placeIntentionalWindowFrame(this, frameGeometry(), target->availableGeometry());
            }
        });
    }
}
#endif

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
    if (m_unique || m_sessionClosed || !m_openDocumentSessionReady || m_restoringOpenDocumentSession || !openDocumentSessionRestoreEnabled()) {
        return;
    }

    m_openDocumentSessionSaveTimer->start(OPEN_DOCUMENT_SESSION_SAVE_DELAY_MS);
}

void Shell::saveOpenDocumentSession()
{
    if (m_unique || m_sessionClosed || !m_openDocumentSessionReady || m_restoringOpenDocumentSession || !openDocumentSessionRestoreEnabled()) {
        return;
    }
#if defined(Q_OS_WIN)
    // Other restored windows may still be opening their files. Their saved
    // groups have no live Shell yet and would otherwise be pruned here.
    if (s_windowsSessionRestoreInProgress) {
        return;
    }
#endif

    KSharedConfigPtr config = ShellSessionConfig();
#if defined(Q_OS_WIN)
    // One process owns the tab endpoint. A timed-out fallback process must not
    // overwrite that owner's session when both are using the same account.
    if (!s_windowsTabOpenPrimaryServer && !QStandardPaths::isTestModeEnabled()) {
        return;
    }

    QList<Shell *> windows;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        Shell *window = qobject_cast<Shell *>(widget);
        if (window && !window->m_unique && window->m_openDocumentSessionReady && !window->m_sessionClosed) {
            windows.append(window);
        }
    }
    std::sort(windows.begin(), windows.end(), [](const Shell *a, const Shell *b) { return a->m_sessionWindowOrder < b->m_sessionWindowOrder; });
    if (windows.isEmpty()) {
        return;
    }

    const QStringList previousIds = savedWindowIds(config);
    QStringList ids;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // Preserve the earlier window order. An indexed window with no ClosedAt
    // marker was part of the completed session, even when its Shell is not
    // currently live (for example while startup is still recovering windows).
    for (const QString &id : previousIds) {
        const auto live = std::find_if(windows.cbegin(), windows.cend(), [&id](const Shell *window) { return window->m_sessionWindowId == id; });
        if (live != windows.cend()) {
            ids.append(id);
        } else {
            const KConfigGroup group = config->group(WindowSessionGroupKey(id));
            const qint64 closedAt = group.readEntry("ClosedAt", qint64(0));
            if ((closedAt == 0 || wasWindowClosedRecently(closedAt, now) || s_failedWindowRestores.contains(id)) && !group.readPathEntry(SESSION_URL_KEY, QStringList()).isEmpty()) {
                ids.append(id);
            }
        }
    }
    for (const Shell *window : std::as_const(windows)) {
        if (!ids.contains(window->m_sessionWindowId)) {
            ids.append(window->m_sessionWindowId);
        }
        KConfigGroup windowGroup = config->group(WindowSessionGroupKey(window->m_sessionWindowId));
        window->writeOpenDocumentSession(windowGroup);
        windowGroup.deleteEntry("ClosedAt");
        windowGroup.writeEntry("IntentionalSeparateWindow", window->m_intentionalSeparateWindow);
        if (window->m_intentionalSeparateWindow) {
            windowGroup.writeEntry("WindowGeometry", window->saveGeometry());
        }
    }
    // Keep the original group as a migration and downgrade-compatible copy of
    // the first window. The index and per-window groups are authoritative.
    KConfigGroup legacyGroup = config->group(OpenDocumentSessionGroupKey());
    windows.first()->writeOpenDocumentSession(legacyGroup);
    KConfigGroup indexGroup = config->group(WindowSessionIndexGroupKey());
    indexGroup.writeEntry("Version", 2);
    indexGroup.writeEntry("WindowIds", ids);
    for (const QString &oldId : previousIds) {
        if (!ids.contains(oldId) && !s_failedWindowRestores.contains(oldId)) {
            config->group(WindowSessionGroupKey(oldId)).deleteGroup();
        }
    }
#else
    KConfigGroup group = config->group(OpenDocumentSessionGroupKey());
    writeOpenDocumentSession(group);
#endif
    saveRecents();
    config->sync();
#if defined(Q_OS_WIN)
    saveSessionRecoveryCopy(config);
#endif
}

bool Shell::restoreOpenDocumentSession()
{
    if (!m_isValid || !openDocumentSessionRestoreEnabled()) {
        return false;
    }

    KSharedConfigPtr config = ShellSessionConfig();
#if defined(Q_OS_WIN)
    const QStringList ids = savedWindowIds(config);
    const bool hasSavedWindow = s_startupWindowSessions.contains(m_sessionWindowId) || ids.contains(m_sessionWindowId);
    const QString groupName = hasSavedWindow ? WindowSessionGroupKey(m_sessionWindowId) : OpenDocumentSessionGroupKey();
#else
    const QString groupName = OpenDocumentSessionGroupKey();
#endif
    const KConfigGroup group = config->group(groupName);
#if defined(Q_OS_WIN)
    const auto saved = s_startupWindowSessions.constFind(m_sessionWindowId);
    const SavedWindowSession session = saved != s_startupWindowSessions.cend() ? *saved : readWindowSession(group);
    const QStringList urls = session.urls;
#else
    const QStringList urls = group.readPathEntry(SESSION_URL_KEY, QStringList());
#endif
    if (urls.isEmpty()) {
#if defined(Q_OS_WIN)
        if (hasSavedWindow) {
            s_failedWindowRestores.insert(m_sessionWindowId);
        }
#endif
        return false;
    }

#if defined(Q_OS_WIN)
    const int desiredTab = session.activeTab;
    const QUrl desiredUrl(session.activeUrl);
#else
    const int desiredTab = group.readEntry<int>(SESSION_TAB_KEY, 0);
    const QUrl desiredUrl(group.readPathEntry(SESSION_ACTIVE_URL_KEY, QString()));
#endif

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

        if (m_tabs[m_tabWidget->currentIndex()].part->url().isEmpty()) {
            openUrl(url);
        } else {
            // A saved tab set must restore into this shell even when new-file
            // tabs are disabled for ordinary opens on another platform.
            openNewTab(url, QString());
        }
        const int openedTab = m_tabWidget->currentIndex();
        if (openedTab < 0 || m_tabs[openedTab].part->url() != url) {
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

#if defined(Q_OS_WIN)
    if (!restoredAny && hasSavedWindow) {
        s_failedWindowRestores.insert(m_sessionWindowId);
        qWarning() << "Could not restore saved Okular window" << m_sessionWindowId;
    }
#endif
    return restoredAny;
}

void Shell::fileOpen()
{
    fileOpenImpl(false);
}

#if defined(Q_OS_WIN)
void Shell::fileOpenInNewWindow()
{
    fileOpenImpl(true);
}

bool Shell::moveTabToNewWindow(int tab, const QPoint &dropPosition)
{
    return moveTabsToWindow({tab}, nullptr, dropPosition);
}

bool Shell::moveTabsToWindow(const QList<int> &tabs, Shell *destination, const QPoint &dropPosition)
{
    if (tabs.isEmpty() || destination == this || (destination && (!destination->isValid() || destination->m_sessionClosed))) {
        return false;
    }
    QList<int> indexes = tabs;
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    for (int tab : std::as_const(indexes)) {
        if (tab < 0 || tab >= m_tabs.size() || m_tabs.at(tab).part->url().isEmpty()) {
            return false;
        }
    }
    // Transfer the existing Parts, including unsaved annotations and reading
    // position. Keep a blank Part in the source if all of its tabs move.
    KParts::ReadWritePart *replacement = nullptr;
    if (indexes.size() == m_tabs.size()) {
        replacement = m_partFactory->create<KParts::ReadWritePart>(this);
        if (!replacement) {
            return false;
        }
    }
    const bool newWindow = !destination;
    if (newWindow) {
        destination = new Shell(QString(), QString(), true);
        if (!destination->isValid()) {
            delete destination;
            delete replacement;
            return false;
        }
    }

    QScreen *targetScreen = newWindow ? (dropPosition.isNull() ? screen() : QGuiApplication::screenAt(dropPosition)) : nullptr;
    QRect targetFrame;
    if (newWindow && !targetScreen) {
        targetScreen = screen() ? screen() : QGuiApplication::primaryScreen();
    }
    if (targetScreen) {
        targetFrame = targetScreen->availableGeometry();
    }

    if (destination->m_tabs.size() == 1 && destination->m_tabs.first().part->url().isEmpty()) {
        KParts::ReadWritePart *emptyPart = destination->m_tabs.first().part;
        auto *emptySide = qobject_cast<Okular::ViewerInterface *>(emptyPart)->getSideContainer();
        destination->createGUI(nullptr);
        destination->m_sidebar->removeWidget(emptySide);
        if (destination->m_showSidebarAction) {
            destination->m_showSidebarAction->disconnect(destination->m_sidebar);
        }
        destination->m_showSidebarAction = nullptr;
        {
            QSignalBlocker blocked(destination->m_tabWidget);
            destination->m_tabWidget->removeTab(0);
            destination->m_tabs.clear();
        }
        if (emptyPart->factory()) {
            emptyPart->factory()->removeClient(emptyPart);
        }
        emptyPart->deleteLater();
    }
    QList<KParts::ReadWritePart *> orderedParts;
    for (int tab : std::as_const(indexes)) {
        orderedParts.append(m_tabs.at(tab).part);
    }
    for (auto it = indexes.crbegin(); it != indexes.crend(); ++it) {
        const int tab = *it;
        const TabState moved = m_tabs.at(tab);
        const QString tabText = m_tabWidget->tabText(tab);
        const QString tabTip = m_tabWidget->tabToolTip(tab);
        const QIcon tabIcon = m_tabWidget->tabIcon(tab);
        auto *movedSide = qobject_cast<Okular::ViewerInterface *>(moved.part)->getSideContainer();
        if (m_showSidebarAction) {
            m_showSidebarAction->disconnect(m_sidebar);
        }
        if (moved.part->factory()) {
            moved.part->factory()->removeClient(moved.part);
        }
        moved.part->disconnect(this);
        disconnect(moved.part);
        m_sidebar->removeWidget(movedSide);
        const int oldActive = m_tabWidget->currentIndex();
        {
            QSignalBlocker blocked(m_tabWidget);
            m_tabWidget->removeTab(tab);
            m_tabs.removeAt(tab);
            if (m_tabs.isEmpty() && replacement) {
                m_tabs.append(TabState(replacement));
                connectPart(replacement);
                m_tabWidget->addTab(replacement->widget(), QString());
                replacement = nullptr;
            }
        }
        moved.part->setParent(destination);
        destination->connectPart(moved.part);
        destination->m_tabs.append(moved);
        const int addedTab = destination->m_tabWidget->addTab(moved.part->widget(), tabIcon, tabText);
        destination->m_tabWidget->setTabToolTip(addedTab, tabTip);
        const int nextActive = oldActive > tab ? oldActive - 1 : qMin(oldActive, m_tabs.size() - 1);
        setActiveTab(nextActive);
    }
    // The reverse removal keeps source indexes valid. Put the destination's
    // tabs back in their original left-to-right order.
    const int firstAdded = destination->m_tabs.size() - orderedParts.size();
    for (int offset = 0; offset < orderedParts.size(); ++offset) {
        destination->m_tabWidget->tabBar()->moveTab(destination->findTabIndex(orderedParts.at(offset)), firstAdded + offset);
    }
    destination->setActiveTab(destination->findTabIndex(orderedParts.last()));
    destination->hideWelcomeScreen();
    if (m_tabs.size() == 1 && m_tabs.first().part->url().isEmpty()) {
        showWelcomeScreen();
    }
    m_nextTabAction->setEnabled(m_tabs.size() > 1);
    m_prevTabAction->setEnabled(m_tabs.size() > 1);
    destination->m_nextTabAction->setEnabled(destination->m_tabs.size() > 1);
    destination->m_prevTabAction->setEnabled(destination->m_tabs.size() > 1);
    updateOpenTabButtonGeometry();
    destination->updateOpenTabButtonGeometry();
    if (newWindow) {
        destination->show();
    }
    if (newWindow && targetScreen) {
        placeIntentionalWindowFrame(destination, targetFrame, targetScreen->availableGeometry());
    }
    destination->saveOpenDocumentSession();
    destination->raise();
    destination->activateWindow();
    return true;
}
#endif

void Shell::fileOpenImpl(bool newWindow)
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
#if defined(Q_OS_WIN)
        Shell *destination = this;
        if (newWindow && !urlList.isEmpty()) {
            destination = new Shell(QString(), QString(), true);
            if (!destination->isValid()) {
                delete destination;
                destination = nullptr;
            } else {
                QScreen *targetScreen = screen() ? screen() : QGuiApplication::primaryScreen();
                destination->show();
                if (targetScreen) {
                    const QRect area = targetScreen->availableGeometry();
                    placeIntentionalWindowFrame(destination, area, area);
                }
            }
        }
        if (destination) {
            for (const QUrl &url : urlList) {
                destination->openDocumentInTab(url, QString());
            }
            if (newWindow) {
                destination->saveOpenDocumentSession();
            }
        }
#else
        Q_UNUSED(newWindow);
        for (const QUrl &url : urlList) {
            openUrl(url);
        }
#endif
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
    if (!m_privateWindowsStartupGeometryApplied && !isFullScreen() && !m_intentionalSeparateWindow) {
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

void Shell::closeEvent(QCloseEvent *event)
{
    KParts::MainWindow::closeEvent(event);
    if (!event->isAccepted()) {
        return;
    }
#if defined(Q_OS_WIN)
    Shell *survivor = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        Shell *window = qobject_cast<Shell *>(widget);
        // Windows can hide all taskbar windows before delivering their close
        // events. A hidden Shell still owns tabs until its close is processed.
        if (window && window != this && !window->m_sessionClosed && !window->m_unique) {
            survivor = window;
            break;
        }
    }
    if (survivor) {
        // Keep this window's remaining tabs through a coordinated shutdown,
        // while allowing a deliberate close to expire after fifteen minutes.
        if (!m_unique && openDocumentSessionRestoreEnabled()) {
            saveOpenDocumentSession();
            const KSharedConfigPtr config = ShellSessionConfig();
            config->group(WindowSessionGroupKey(m_sessionWindowId)).writeEntry("ClosedAt", QDateTime::currentSecsSinceEpoch());
            config->sync();
        }
        if (m_windowsTabOpenServer) {
            // Keep the primary pipe alive after closing its original window.
            m_windowsTabOpenServer->disconnect(this);
            m_windowsTabOpenServer->setParent(survivor);
            survivor->m_windowsTabOpenServer = m_windowsTabOpenServer;
            connect(m_windowsTabOpenServer, &QLocalServer::newConnection, survivor, &Shell::handleWindowsTabOpenConnection);
            m_windowsTabOpenServer = nullptr;
            if (m_windowsBatchOpenSummaryTimer->isActive()) {
                survivor->m_windowsBatchOpenedCount += m_windowsBatchOpenedCount;
                survivor->m_windowsBatchAlreadyOpenCount += m_windowsBatchAlreadyOpenCount;
                survivor->m_windowsBatchSummaryTarget = m_windowsBatchSummaryTarget;
                survivor->m_windowsBatchOpenSummaryTimer->start(m_windowsBatchOpenSummaryTimer->remainingTime());
                m_windowsBatchOpenSummaryTimer->stop();
            }
        }
        m_sessionClosed = true;
        if (m_openDocumentSessionSaveTimer) {
            m_openDocumentSessionSaveTimer->stop();
        }
        survivor->saveOpenDocumentSession();
    } else {
        saveOpenDocumentSession();
        // The last window completes a coordinated shutdown. Windows closed
        // shortly before it are part of that finished session, even if the
        // user does not start Okular again until much later.
        if (!m_unique && openDocumentSessionRestoreEnabled() && (s_windowsTabOpenPrimaryServer || QStandardPaths::isTestModeEnabled())) {
            const KSharedConfigPtr config = ShellSessionConfig();
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            bool changed = false;
            for (const QString &id : savedWindowIds(config)) {
                KConfigGroup group = config->group(WindowSessionGroupKey(id));
                const qint64 closedAt = group.readEntry("ClosedAt", qint64(0));
                if (wasWindowClosedRecently(closedAt, now)) {
                    group.deleteEntry("ClosedAt");
                    changed = true;
                }
            }
            if (changed) {
                config->sync();
                saveSessionRecoveryCopy(config);
            }
        }
        m_sessionClosed = true;
        if (m_openDocumentSessionSaveTimer) {
            m_openDocumentSessionSaveTimer->stop();
        }
    }
#endif
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

bool Shell::closeTab(int tab)
{
    if (tab < 0 || tab >= m_tabs.size()) {
        return false;
    }
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
    return closeSuccess;
}

void Shell::openNewTab(const QUrl &url, const QString &serializedOptions)
{
    const int previousActiveTab = m_tabWidget->currentIndex();

    hideWelcomeScreen();

    bool activateTabIfAlreadyOpen = true;
#if !defined(Q_OS_WIN)
    KParts::ReadWritePart *const activePart = m_tabs[previousActiveTab].part;
    QMetaObject::invokeMethod(activePart, "activateTabIfAlreadyOpenFile", Q_RETURN_ARG(bool, activateTabIfAlreadyOpen));
#endif

    if (activateTabIfAlreadyOpen) {
        const int tabIndex = findTabIndex(url);

        if (tabIndex >= 0) {
            setActiveTab(tabIndex);
            addRecentUrl(url);
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
        addRecentUrl(url);
        scheduleOpenDocumentSessionSave();
    } else {
        setActiveTab(previousActiveTab);
        closeTab(m_tabs.size() - 1);
        m_recent->removeUrl(url);
        saveRecents();
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
    auto it = std::find_if(m_tabs.begin(), m_tabs.end(), [&url](const TabState &state) { return state.part->url() == url; });
#if defined(Q_OS_WIN)
    // Explorer and session entries can spell the same Windows path differently.
    // Compare resolved paths only after the cheap URL match fails; distinct
    // copies of a PDF remain separate documents.
    if (it == m_tabs.end() && url.isLocalFile()) {
        const QString canonicalPath = QFileInfo(url.toLocalFile()).canonicalFilePath();
        if (!canonicalPath.isEmpty()) {
            it = std::find_if(m_tabs.begin(), m_tabs.end(), [&canonicalPath](const TabState &state) {
                const QUrl openUrl = state.part->url();
                return openUrl.isLocalFile() && QFileInfo(openUrl.toLocalFile()).canonicalFilePath().compare(canonicalPath, Qt::CaseInsensitive) == 0;
            });
        }
    }
#endif
    return (it != m_tabs.end()) ? std::distance(m_tabs.begin(), it) : -1;
}

#if defined(Q_OS_WIN)
Shell *Shell::otherWindowWithUrl(const QUrl &url) const
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        Shell *other = qobject_cast<Shell *>(widget);
        if (other && other != this && other->isVisible() && !other->m_sessionClosed && !other->m_unique && other->findTabIndex(url) >= 0) {
            return other;
        }
    }
    return nullptr;
}
#endif

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

void Shell::centerActiveTabInTabBar()
{
    if (!m_tabWidget || m_tabWidget->currentIndex() < 0) {
        return;
    }

    QTabBar *const tabBar = m_tabWidget->tabBar();
    const int currentIndex = m_tabWidget->currentIndex();
    tabBar->setCurrentIndex(currentIndex);

    QList<QToolButton *> scrollButtons = tabBar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    scrollButtons.removeAll(m_centerActiveTabButton);
    scrollButtons.erase(std::remove_if(scrollButtons.begin(), scrollButtons.end(), [](const QToolButton *button) {
                            return !button || !button->isVisible() || button->geometry().isEmpty();
                        }),
                        scrollButtons.end());
    std::sort(scrollButtons.begin(), scrollButtons.end(), [](const QToolButton *a, const QToolButton *b) {
        return a->geometry().x() < b->geometry().x();
    });

    if (scrollButtons.size() >= 2) {
        m_centeringActiveTab = true;
        QToolButton *const leftScrollButton = scrollButtons.at(scrollButtons.size() - 2);
        QToolButton *const rightScrollButton = scrollButtons.at(scrollButtons.size() - 1);
        const bool rightToLeft = tabBar->layoutDirection() == Qt::RightToLeft;
        const int visibleLeft = rightToLeft ? rightScrollButton->geometry().right() + 1 : tabBar->rect().left();
        const int visibleRight = rightToLeft ? tabBar->rect().right() : leftScrollButton->geometry().left() - 1;
        const int visibleCenter = (visibleLeft + visibleRight) / 2;
        const int tolerance = std::max(1, tabBar->tabRect(currentIndex).width() / 4);

        for (int i = 0; i < tabBar->count(); ++i) {
            const QRect currentRect = tabBar->tabRect(currentIndex);
            const int displacement = currentRect.center().x() - visibleCenter;
            if (std::abs(displacement) <= tolerance) {
                break;
            }

            QToolButton *const scrollButton = displacement < 0 ? leftScrollButton : rightScrollButton;
            if (!scrollButton->isEnabled()) {
                break;
            }
            const int previousCenter = currentRect.center().x();
            scrollButton->click();
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            const int newCenter = tabBar->tabRect(currentIndex).center().x();
            if (newCenter == previousCenter) {
                break;
            }
            if (std::abs(newCenter - visibleCenter) >= std::abs(displacement)) {
                QToolButton *const reverseButton = displacement < 0 ? rightScrollButton : leftScrollButton;
                reverseButton->click();
                break;
            }
        }
        m_centeringActiveTab = false;
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
    scrollButtons.removeAll(m_centerActiveTabButton);
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
        if (!leftScrollButton->property("okularSelectsTab").toBool()) {
            leftScrollButton->setProperty("okularSelectsTab", true);
            connect(leftScrollButton, &QToolButton::clicked, this, [this]() {
                if (!m_centeringActiveTab) {
                    const int step = m_tabWidget->tabBar()->layoutDirection() == Qt::RightToLeft ? 1 : -1;
                    setActiveTab(m_tabWidget->currentIndex() + step);
                    updateOpenTabButtonGeometry();
                }
            });
        }
        if (!rightScrollButton->property("okularSelectsTab").toBool()) {
            rightScrollButton->setProperty("okularSelectsTab", true);
            connect(rightScrollButton, &QToolButton::clicked, this, [this]() {
                if (!m_centeringActiveTab) {
                    const int step = m_tabWidget->tabBar()->layoutDirection() == Qt::RightToLeft ? -1 : 1;
                    setActiveTab(m_tabWidget->currentIndex() + step);
                    updateOpenTabButtonGeometry();
                }
            });
        }
        const QRect leftRect = leftScrollButton->geometry();
        const QRect rightRect = rightScrollButton->geometry();
        // The tab-bar style reserves space for all three controls.
        // Keep them inside that space so the first arrow cannot cover a tab.
        const int buttonWidth = m_centerActiveTabButton->width();
        const int leftX = leftRect.x();
        const int centerX = leftX + buttonWidth;
        const int rightX = centerX + buttonWidth;
        leftScrollButton->setGeometry(leftX, rightRect.y(), buttonWidth, rightRect.height());
        m_centerActiveTabButton->setGeometry(centerX, rightRect.y(), buttonWidth, rightRect.height());
        rightScrollButton->setGeometry(rightX, rightRect.y(), buttonWidth, rightRect.height());
        const bool rightToLeft = tabBar->layoutDirection() == Qt::RightToLeft;
        leftScrollButton->setEnabled(rightToLeft ? m_tabWidget->currentIndex() < m_tabWidget->count() - 1 : m_tabWidget->currentIndex() > 0);
        rightScrollButton->setEnabled(rightToLeft ? m_tabWidget->currentIndex() > 0 : m_tabWidget->currentIndex() < m_tabWidget->count() - 1);
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
    if (m_centralStackedWidget->currentWidget() == m_welcomeScreen) {
        m_centralStackedWidget->setCurrentWidget(m_tabWidget);
    }
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

void Shell::forgetRecentItems(const QList<QUrl> &urls)
{
    if (m_recent != nullptr) {
        for (const QUrl &url : urls) {
            m_recent->removeUrl(url);
        }
        refreshRecentsOnWelcomeScreen();
    }
}

void Shell::triggerUpdateRecentItems(const int maxItems)
{
    m_recent->setMaxItems(std::clamp(maxItems, 0, MAX_RECENT_FILES));
    m_welcomeScreen->setMaxRecentItems(m_recent->maxItems());
    // saveRecents() dumps the recent files in correct order to KConfigGroup, respecting the allowed no. of recent items, discarding older items if needed
    refreshRecentsOnWelcomeScreen();
    m_recent->loadEntries(ShellSessionConfig()->group(RecentFilesGroupKey()));
}

void Shell::readRecentFilesSettings()
{
    // Read no. of max. recent items from okularpartrc, populate File->Open Recent menu-item as well as recentsListView on welcome screen
    QString configFilePath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QLatin1Char('/') + QLatin1String("okularpartrc");
    const KConfigGroup confgrp = KSharedConfig::openConfig(configFilePath).data()->group(QStringLiteral("General"));
    const int defaultMaxRecentItems = MAX_RECENT_FILES;
    int maxRecentItems = confgrp.readEntry<int>("MaxRecentItems", defaultMaxRecentItems);
    m_recent->setMaxItems(std::clamp(maxRecentItems, 0, MAX_RECENT_FILES));
    m_welcomeScreen->setMaxRecentItems(m_recent->maxItems());
    m_welcomeScreen->loadRecents();
    m_recent->loadEntries(ShellSessionConfig()->group(RecentFilesGroupKey()));
    pruneRecentFiles();
}

#include "moc_shell.cpp"
#include "shell.moc"
