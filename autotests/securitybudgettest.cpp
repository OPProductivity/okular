/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "core/documentlimits_p.h"
#include "core/processbudget_p.h"
#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <cstdio>

class SecurityBudgetTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void boundedCopy()
    {
        QByteArray input(1025, 'x');
        QBuffer source(&input);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QByteArray result;
        QBuffer dest(&result);
        QVERIFY(dest.open(QIODevice::WriteOnly));
        QVERIFY(!Okular::copyBoundedDocument(&source, &dest, 1024));
        QVERIFY(result.size() <= 1024);
        source.seek(0);
        QVERIFY(Okular::copyBoundedDocument(&source, &dest, 1025));
        QCOMPARE(result, input);
        QCOMPARE(Okular::expansionLimit(1), 64LL * 1024 * 1024);
        QCOMPARE(Okular::expansionLimit(1024LL * 1024 * 1024), Okular::MaxExpandedDocumentBytes);
    }
    void helperCompletes()
    {
        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments({QStringLiteral("--helper-ok")});
        QByteArray output, errors;
        QCOMPARE(Okular::runBoundedHelper(process, output, errors, 5000), 0);
        QCOMPARE(output, QByteArray("ready"));
    }
    void helperExitCode()
    {
        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments({QStringLiteral("--helper-fail")});
        QByteArray output, errors;
        QCOMPARE(Okular::runBoundedHelper(process, output, errors, 5000), 7);
    }
    void helperTimesOut()
    {
        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments({QStringLiteral("--helper-spin")});
        QByteArray output, errors;
        QElapsedTimer elapsed;
        elapsed.start();
        QCOMPARE(Okular::runBoundedHelper(process, output, errors, 300), -1);
        QCOMPARE(process.state(), QProcess::NotRunning);
        QVERIFY(elapsed.elapsed() < 5000);
    }
    void helperOutputBudget()
    {
        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments({QStringLiteral("--helper-output")});
        QByteArray output, errors;
        QCOMPARE(Okular::runBoundedHelper(process, output, errors, 5000), -1);
        QVERIFY(output.size() <= 8 * 1024 * 1024);
    }
    void helperDescendants()
    {
        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments({QStringLiteral("--helper-tree")});
        QByteArray output, errors;
        QCOMPARE(Okular::runBoundedHelper(process, output, errors, 1000), -1);
        bool ok = false;
        const qint64 pid = output.trimmed().toLongLong(&ok);
        QVERIFY(ok && pid > 0);
#ifdef Q_OS_WIN
        HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
        if (child) {
            QCOMPARE(WaitForSingleObject(child, 3000), DWORD(WAIT_OBJECT_0));
            CloseHandle(child);
        }
#else
        // A dead child can remain a zombie briefly until the OS reaps it.
        QVERIFY(::kill(pid, SIGCONT) != 0 || process.state() == QProcess::NotRunning);
#endif
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString mode = app.arguments().value(1);
    if (mode == QLatin1String("--helper-ok")) {
        std::fwrite("ready", 1, 5, stdout);
        return 0;
    }
    if (mode == QLatin1String("--helper-fail")) {
        return 7;
    }
    if (mode == QLatin1String("--helper-output")) {
        const QByteArray chunk(1024 * 1024, 'x');
        for (int i = 0; i < 10; ++i) {
            std::fwrite(chunk.constData(), 1, chunk.size(), stdout);
        }
        return 0;
    }
    if (mode == QLatin1String("--helper-tree")) {
        QProcess child;
        child.start(app.applicationFilePath(), {QStringLiteral("--helper-spin")});
        if (!child.waitForStarted()) {
            return 1;
        }
        std::fprintf(stdout, "%lld\n", static_cast<long long>(child.processId()));
        std::fflush(stdout);
        QThread::sleep(60);
        return 0;
    }
    if (mode == QLatin1String("--helper-spin")) {
        QThread::sleep(60);
        return 0;
    }
    SecurityBudgetTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "securitybudgettest.moc"
