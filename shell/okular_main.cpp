/*
    SPDX-FileCopyrightText: 2002 Wilco Greven <greven@kde.org>
    SPDX-FileCopyrightText: 2003 Christophe Devriese <Christophe.Devriese@student.kuleuven.ac.be>
    SPDX-FileCopyrightText: 2003 Laurent Montel <montel@kde.org>
    SPDX-FileCopyrightText: 2003-2007 Albert Astals Cid <aacid@kde.org>
    SPDX-FileCopyrightText: 2004 Andy Goossens <andygoossens@telenet.be>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "okular_main.h"

#include "aboutdata.h"
#include "shell.h"
#include "shellutils.h"
#include <KLocalizedString>
#include <KWindowSystem>
#include <QApplication>
#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QLockFile>
#include <QLocalSocket>
#include <QMimeData>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>
#include <QThread>

#include "config-okular.h"
#if HAVE_X11
#include <KX11Extras>
#include <private/qtx11extras_p.h>
#endif
#if HAVE_DBUS
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#endif // HAVE_DBUS

#include <iostream>
#include <memory>

static bool shouldRestoreOpenDocumentSession(const QString &serializedOptions)
{
    return !ShellUtils::unique(serializedOptions) && !ShellUtils::noRaise(serializedOptions) && !ShellUtils::startInPresentation(serializedOptions)
        && !ShellUtils::showPrintDialog(serializedOptions) && !ShellUtils::showPrintDialogAndExit(serializedOptions) && ShellUtils::page(serializedOptions).isEmpty()
        && ShellUtils::find(serializedOptions).isEmpty() && ShellUtils::editorCmd(serializedOptions).isEmpty();
}

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static constexpr quint32 WINDOWS_TAB_OPEN_MAGIC = 0x4f4b5450; // OKTP

static QString WindowsTabOpenStartupLockPath()
{
    return QDir::temp().filePath(ShellUtils::windowsTabOpenServerName() + QStringLiteral(".lock"));
}

static std::unique_ptr<QLockFile> s_windowsTabOpenStartupLock;
static HANDLE s_windowsTabOpenPrimaryMutex = nullptr;
static bool s_windowsTabOpenPrimaryMutexOwned = false;

static bool tryBecomeWindowsTabOpenPrimary()
{
    if (s_windowsTabOpenPrimaryMutexOwned) {
        return true;
    }

    if (!s_windowsTabOpenPrimaryMutex) {
        const std::wstring mutexName = QString(QStringLiteral(R"(Local\)") + ShellUtils::windowsTabOpenServerName() + QStringLiteral("-primary")).toStdWString();
        s_windowsTabOpenPrimaryMutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    }

    if (!s_windowsTabOpenPrimaryMutex) {
        return true;
    }

    const DWORD waitResult = WaitForSingleObject(s_windowsTabOpenPrimaryMutex, 0);
    s_windowsTabOpenPrimaryMutexOwned = waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED;
    return s_windowsTabOpenPrimaryMutexOwned;
}

static bool shouldAttachExistingWindowsInstance(const QStringList &paths, const QString &serializedOptions)
{
    if (QStandardPaths::isTestModeEnabled() && qEnvironmentVariableIsEmpty("OKULAR_TEST_INSTANCE")) {
        return false;
    }
    return !ShellUtils::unique(serializedOptions) && !ShellUtils::showPrintDialogAndExit(serializedOptions) && ShellUtils::editorCmd(serializedOptions).isEmpty()
        && (!paths.isEmpty() || shouldRestoreOpenDocumentSession(serializedOptions));
}

static bool shouldHoldWindowsStartupLock(const QStringList &paths, const QString &serializedOptions)
{
    return (!QStandardPaths::isTestModeEnabled() || !qEnvironmentVariableIsEmpty("OKULAR_TEST_INSTANCE")) && paths.isEmpty()
        && shouldRestoreOpenDocumentSession(serializedOptions);
}

static bool sendWindowsTabOpenRequest(const QStringList &paths, const QString &serializedOptions, int timeoutMs)
{
    if (!ShellUtils::noRaise(serializedOptions)) {
        AllowSetForegroundWindow(ASFW_ANY);
    }

    QLocalSocket socket;
    socket.connectToServer(ShellUtils::windowsTabOpenServerName(), QIODevice::ReadWrite);
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << WINDOWS_TAB_OPEN_MAGIC << serializedOptions << paths;

    if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(1000)) {
        return false;
    }

    if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(5000)) {
        return false;
    }
    if (socket.read(1) != QByteArrayLiteral("A")) {
        return false;
    }

    socket.disconnectFromServer();
    return true;
}

static bool waitForExistingWindowsInstance(const QStringList &paths, const QString &serializedOptions, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        if (sendWindowsTabOpenRequest(paths, serializedOptions, 250)) {
            return true;
        }
        QThread::msleep(50);
    }

    return false;
}

static bool attachExistingWindowsInstance(const QStringList &paths, const QString &serializedOptions)
{
    if (!shouldAttachExistingWindowsInstance(paths, serializedOptions)) {
        return false;
    }

    if (sendWindowsTabOpenRequest(paths, serializedOptions, 500)) {
        return true;
    }

    auto startupLock = std::make_unique<QLockFile>(WindowsTabOpenStartupLockPath());
    startupLock->setStaleLockTime(10000);
    if (tryBecomeWindowsTabOpenPrimary()) {
        if (waitForExistingWindowsInstance(paths, serializedOptions, 1500)) {
            return true;
        }
        if (startupLock->tryLock(0)) {
            s_windowsTabOpenStartupLock = std::move(startupLock);
        }
        return false;
    }

    if (waitForExistingWindowsInstance(paths, serializedOptions, 60000)) {
        return true;
    }

    if (tryBecomeWindowsTabOpenPrimary() && startupLock->tryLock(0)) {
        s_windowsTabOpenStartupLock = std::move(startupLock);
        return false;
    }

    // No instance confirmed receipt. Open the requested file in this process.
    return false;
}
#endif

#if HAVE_DBUS
static QString startupId()
{
    QString result;
    if (KWindowSystem::isPlatformWayland()) {
        result = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
        qunsetenv("XDG_ACTIVATION_TOKEN");
    } else if (KWindowSystem::isPlatformX11()) {
#if HAVE_X11
        result = QString::fromUtf8(QX11Info::nextStartupId());
#endif
    }

    return result;
}
#endif

static bool attachUniqueInstance(const QStringList &paths, const QString &serializedOptions)
{
#if HAVE_DBUS
    if (!ShellUtils::unique(serializedOptions) || paths.count() != 1) {
        return false;
    }

    QDBusInterface iface(QStringLiteral("org.kde.okular"), QStringLiteral("/okularshell"), QStringLiteral("org.kde.okular"));
    if (!iface.isValid()) {
        return false;
    }

    if (!ShellUtils::editorCmd(serializedOptions).isEmpty()) {
        QString message =
            i18n("You cannot set the editor command in an already running okular instance. Please disable the tabs and try again. Please note, that unique is also not supported when setting the editor command at the commandline.\n");
        std::cerr << message.toStdString();
        exit(1);
    }

    const QString page = ShellUtils::page(serializedOptions);
    iface.call(QStringLiteral("openDocument"), ShellUtils::urlFromArg(paths[0], ShellUtils::qfileExistFunc(), page).url(), serializedOptions);
    if (!ShellUtils::noRaise(serializedOptions)) {
        iface.call(QStringLiteral("tryRaise"), startupId());
    }

    return true;
#else  // HAVE_DBUS
    Q_UNUSED(paths);
    Q_UNUSED(serializedOptions);
    return false;
#endif // HAVE_DBUS
}

// Ask an existing non-unique instance to open new tabs
static bool attachExistingInstance(const QStringList &paths, const QString &serializedOptions)
{
#if HAVE_DBUS
    if (paths.count() < 1) {
        return false;
    }

    // Don't try to attach to an existing instance with --print-and-exit because that would mean
    // we're going to exit that other instance and that's just rude
    if (ShellUtils::showPrintDialogAndExit(serializedOptions)) {
        return false;
    }

    // If DBus isn't running, we can't attach to an existing instance.
    auto *sessionInterface = QDBusConnection::sessionBus().interface();
    if (!sessionInterface) {
        return false;
    }

    const QStringList services = sessionInterface->registeredServiceNames().value();

    QScopedPointer<QDBusInterface> bestService;
#if HAVE_X11
    const int desktop = KWindowSystem::isPlatformX11() ? KX11Extras::currentDesktop() : 0;
#else
    const int desktop = 0;
#endif

    // Select the first instance that isn't us (metric may change in future)
    const QString ownDbus = ShellUtils::currentProcessDbusName();
    for (const QString &service : services) {
        if (service.startsWith(ShellUtils::kPerProcessDbusPrefix) && service != ownDbus) {
            bestService.reset(new QDBusInterface(service, QStringLiteral("/okularshell"), QStringLiteral("org.kde.okular")));

            // Find a window that can handle our documents
            const QDBusReply<bool> reply = bestService->call(QStringLiteral("canOpenDocs"), (int)paths.count(), desktop);
            if (reply.isValid() && reply.value()) {
                break;
            }

            bestService.reset();
        }
    }

    if (!bestService) {
        return false;
    }

    for (const QString &arg : paths) {
        // Copy stdin to temporary file which can be opened by the existing
        // window. The temp file is automatically deleted after it has been
        // opened. Not sure if this behavior is safe on all platforms.
        QScopedPointer<QTemporaryFile> tempFile;
        QString path;
        if (arg == QLatin1String("-")) {
            tempFile.reset(new QTemporaryFile);
            QFile stdinFile;
            if (!tempFile->open() || !stdinFile.open(stdin, QIODevice::ReadOnly)) {
                return false;
            }

            const size_t bufSize = 1024 * 1024;
            QScopedPointer<char, QScopedPointerArrayDeleter<char>> buf(new char[bufSize]);
            size_t bytes;
            do {
                bytes = stdinFile.read(buf.data(), bufSize);
                tempFile->write(buf.data(), bytes);
            } while (bytes != 0);

            path = tempFile->fileName();
        } else {
            // Page only makes sense if we are opening one file
            const QString page = ShellUtils::page(serializedOptions);
            path = ShellUtils::urlFromArg(arg, ShellUtils::qfileExistFunc(), page).url();
        }

        // Returns false if it can't fit another document
        const QDBusReply<bool> reply = bestService->call(QStringLiteral("openDocument"), path, serializedOptions);
        if (!reply.isValid() || !reply.value()) {
            return false;
        }
    }

    if (!ShellUtils::editorCmd(serializedOptions).isEmpty()) {
        QString message(
            i18n("You cannot set the editor command in an already running okular instance. Please disable the tabs and try again. Please note, that unique is also not supported when setting the editor command at the commandline.\n"));
        std::cerr << message.toStdString();
        exit(1);
    }

    bestService->call(QStringLiteral("tryRaise"), startupId());

    return true;
#else  // HAVE_DBUS
    Q_UNUSED(paths);
    Q_UNUSED(serializedOptions);
    return false;
#endif // HAVE_DBUS
}

namespace Okular
{
Status main(const QStringList &paths, const QString &serializedOptions)
{
    if (ShellUtils::unique(serializedOptions) && paths.count() > 1) {
        QTextStream stream(stderr);
        stream << i18n("Error: Can't open more than one document with the --unique switch") << '\n';
        return Error;
    }

    if (ShellUtils::startInPresentation(serializedOptions) && paths.count() > 1) {
        QTextStream stream(stderr);
        stream << i18n("Error: Can't open more than one document with the --presentation switch") << '\n';
        return Error;
    }

    if (ShellUtils::showPrintDialog(serializedOptions) && paths.count() > 1) {
        QTextStream stream(stderr);
        stream << i18n("Error: Can't open more than one document with the --print switch") << '\n';
        return Error;
    }

    if (!ShellUtils::page(serializedOptions).isEmpty() && paths.count() > 1) {
        QTextStream stream(stderr);
        stream << i18n("Error: Can't open more than one document with the --page switch") << '\n';
        return Error;
    }

    if (!ShellUtils::find(serializedOptions).isEmpty() && paths.count() > 1) {
        QTextStream stream(stderr);
        stream << i18n("Error: Can't open more than one document with the --find switch") << '\n';
        return Error;
    }

    // try to attach to existing session, unique or not
    if (attachUniqueInstance(paths, serializedOptions)
#if defined(Q_OS_WIN)
        || attachExistingWindowsInstance(paths, serializedOptions)
#endif
        || attachExistingInstance(paths, serializedOptions)) {
        return AttachedOtherProcess;
    }

#if defined(Q_OS_WIN)
    std::unique_ptr<QLockFile> windowsStartupLock;
    if (shouldHoldWindowsStartupLock(paths, serializedOptions)) {
        tryBecomeWindowsTabOpenPrimary();
        windowsStartupLock = std::make_unique<QLockFile>(WindowsTabOpenStartupLockPath());
        windowsStartupLock->setStaleLockTime(10000);
        windowsStartupLock->tryLock(0);
    }
#endif

    Shell *shell = new Shell(serializedOptions);
    if (!shell->isValid()) {
        return Error;
    }

    if (!paths.isEmpty()) {
        shell->prepareForDocumentOpen();
    }
    shell->show();
    const bool restoredOpenDocumentSession = shouldRestoreOpenDocumentSession(serializedOptions) && shell->restoreOpenDocumentSession();

#if defined(Q_OS_WIN)
    windowsStartupLock.reset();
#endif

    for (int i = 0; i < paths.count();) {
        // Page only makes sense if we are opening one file
        const QString page = ShellUtils::page(serializedOptions);
        const QUrl url = ShellUtils::urlFromArg(paths[i], ShellUtils::qfileExistFunc(), page);
        if ((restoredOpenDocumentSession ? shell->openDocumentInTab(url, serializedOptions) : shell->openDocument(url, serializedOptions))) {
            ++i;
        } else {
            shell = new Shell(serializedOptions);
            if (!shell->isValid()) {
                return Error;
            }
            shell->show();
        }
    }

    return Success;
}

}
