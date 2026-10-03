/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef OKULAR_PROCESSBUDGET_P_H
#define OKULAR_PROCESSBUDGET_P_H

#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStorageInfo>
#include <QTimer>
#include <functional>
#include <memory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace Okular
{
// Own helper descendants too: killing only the immediate process is insufficient.
class HelperProcessTree
{
public:
    explicit HelperProcessTree(QProcess &process)
        : m_process(process)
    {
#ifdef Q_OS_WIN
        m_job = CreateJobObjectW(nullptr, nullptr);
        if (m_job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info {};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_JOB_MEMORY;
            info.JobMemoryLimit = SIZE_T(1024ULL * 1024 * 1024);
            if (!SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
                CloseHandle(m_job);
                m_job = nullptr;
            }
        }
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#else
        process.setChildProcessModifier([]() {
            const rlimit memory {1024ULL * 1024 * 1024, 1024ULL * 1024 * 1024};
            const rlimit file {1024ULL * 1024 * 1024, 1024ULL * 1024 * 1024};
            const rlimit cpu {120, 120};
            if (::setrlimit(RLIMIT_AS, &memory) != 0 || ::setrlimit(RLIMIT_FSIZE, &file) != 0 || ::setrlimit(RLIMIT_CPU, &cpu) != 0 || ::setsid() < 0) {
                ::_exit(127);
            }
        });
#endif
    }
    bool attach()
    {
        m_pid = m_process.processId();
#ifdef Q_OS_WIN
        HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(m_pid));
        const bool ok = m_job && child && AssignProcessToJobObject(m_job, child);
        if (child) {
            CloseHandle(child);
        }
        return ok;
#else
        return m_pid > 0;
#endif
    }
    void terminate()
    {
#ifdef Q_OS_WIN
        if (m_job) {
            TerminateJobObject(m_job, 1);
        }
#else
        if (m_pid > 0) {
            ::kill(-m_pid, SIGKILL);
        }
#endif
        m_process.kill();
    }
    void kill()
    {
        terminate();
        m_process.waitForFinished(5000);
    }
    ~HelperProcessTree()
    {
#ifdef Q_OS_WIN
        if (m_job) {
            CloseHandle(m_job);
        }
#else
        if (m_pid > 0) {
            ::kill(-m_pid, SIGKILL);
        }
#endif
    }
    HelperProcessTree(const HelperProcessTree &) = delete;
    HelperProcessTree &operator=(const HelperProcessTree &) = delete;

private:
    QProcess &m_process;
    qint64 m_pid = 0;
#ifdef Q_OS_WIN
    HANDLE m_job = nullptr;
#endif
};

// Install before start(). The timer is owned by the process, so closing its owner
// cancels the complete helper tree without leaving a detached conversion running.
inline void boundAsyncHelper(QProcess *process, const QString &outputFile = {}, int timeoutMs = 120000)
{
    auto tree = std::make_shared<HelperProcessTree>(*process);
    auto timer = new QTimer(process);
    timer->setInterval(50);
    auto elapsed = std::make_shared<QElapsedTimer>();
    auto bytes = std::make_shared<qint64>(0);
    QObject::connect(process, &QProcess::started, timer, [process, tree, timer, elapsed]() {
        if (!tree->attach()) {
            tree->terminate();
            return;
        }
        elapsed->start();
        timer->start();
    });
    QObject::connect(timer, &QTimer::timeout, timer, [process, tree, timer, elapsed, bytes, outputFile, timeoutMs]() {
        *bytes += process->readAllStandardOutput().size() + process->readAllStandardError().size();
        const QFileInfo output(outputFile);
        const QStorageInfo storage(output.absolutePath());
        const bool tooLarge = !outputFile.isEmpty() && (output.size() > 1024LL * 1024 * 1024 || (storage.isValid() && storage.bytesAvailable() < 16 * 1024 * 1024));
        if (*bytes > 8 * 1024 * 1024 || elapsed->elapsed() >= timeoutMs || tooLarge) {
            timer->stop();
            tree->terminate();
        }
    });
    QObject::connect(process, &QProcess::finished, timer, [timer, tree](int, QProcess::ExitStatus) { timer->stop(); });
}

// The monitor may additionally bound generated files or extracted archives.
inline int runBoundedHelper(QProcess &process, QByteArray &output, QByteArray &errors, int timeoutMs = 120000, const std::function<bool()> &monitor = {})
{
    HelperProcessTree tree(process);
    QElapsedTimer elapsed;
    elapsed.start();
    process.start();
    if (!process.waitForStarted(qMin(timeoutMs, 5000))) {
        tree.kill();
        return -1;
    }
    if (!tree.attach()) {
        tree.kill();
        return -1;
    }
    constexpr qsizetype maxOutput = 8 * 1024 * 1024;
    auto drain = [&]() {
        const QByteArray out = process.readAllStandardOutput();
        const QByteArray err = process.readAllStandardError();
        if (out.size() > maxOutput - output.size() || err.size() > maxOutput - errors.size()) {
            return false;
        }
        output += out;
        errors += err;
        return true;
    };
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(50);
        if (!drain() || elapsed.elapsed() >= timeoutMs || (monitor && !monitor())) {
            tree.kill();
            return -1;
        }
    }
    if (!drain() || (monitor && !monitor())) {
        tree.kill();
        return -1;
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}
}
#endif
