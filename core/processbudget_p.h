/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef OKULAR_PROCESSBUDGET_P_H
#define OKULAR_PROCESSBUDGET_P_H

#include <QElapsedTimer>
#include <QProcess>
#include <functional>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <signal.h>
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
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
                CloseHandle(m_job);
                m_job = nullptr;
            }
        }
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#else
        process.setChildProcessModifier([]() {
            if (::setsid() < 0) {
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
    void kill()
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
