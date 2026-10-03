/*
    SPDX-FileCopyrightText: 2007 Tobias Koenig <tokoe@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "unrar.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGlobalStatic>
#include <QLoggingCategory>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <core/documentlimits_p.h>
#include <core/processbudget_p.h>

#include "debug_comicbook.h"

#include <QRegularExpression>
#include <QStandardPaths>
#include <memory>

struct UnrarHelper {
    UnrarHelper();
    ~UnrarHelper();

    UnrarHelper(const UnrarHelper &) = delete;
    UnrarHelper &operator=(const UnrarHelper &) = delete;

    UnrarFlavour *kind;
    QString unrarPath;
    QString lsarPath;
};

Q_GLOBAL_STATIC(UnrarHelper, helper)

static UnrarFlavour *detectUnrar(const QString &unrarPath, const QString &versionCommand)
{
    UnrarFlavour *kind = nullptr;
    QProcess proc;
    if (unrarPath.isEmpty()) {
        return nullptr;
    }
    proc.setProgram(unrarPath);
    proc.setArguments({versionCommand});
    QByteArray stdoutData;
    QByteArray stderrData;
    if (Okular::runBoundedHelper(proc, stdoutData, stderrData, 5000) < 0) {
        return nullptr;
    }
    static const QRegularExpression regex(QStringLiteral("[\r\n]"));
    const QString output = QString::fromLocal8Bit(stdoutData);
    const QList<QStringView> lines = QStringView(output).split(regex, Qt::SkipEmptyParts);
    if (!lines.isEmpty()) {
        if (lines.first().startsWith(QLatin1String("UNRAR "))) {
            kind = new NonFreeUnrarFlavour();
        } else if (lines.first().startsWith(QLatin1String("RAR "))) {
            kind = new NonFreeUnrarFlavour();
        } else if (lines.first().startsWith(QLatin1String("unrar "))) {
            kind = new FreeUnrarFlavour();
        } else if (lines.first().startsWith(QLatin1String("v"))) {
            kind = new UnarFlavour();
        }
    }
    return kind;
}

UnrarHelper::UnrarHelper()
    : kind(nullptr)
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("lsar"));

    if (!path.isEmpty()) {
        lsarPath = path;
    }

    path = QStandardPaths::findExecutable(QStringLiteral("unrar-nonfree"));

    if (path.isEmpty()) {
        path = QStandardPaths::findExecutable(QStringLiteral("unrar"));
    }
    if (path.isEmpty()) {
        path = QStandardPaths::findExecutable(QStringLiteral("rar"));
    }
    if (path.isEmpty()) {
        path = QStandardPaths::findExecutable(QStringLiteral("unar"));
    }

    if (!path.isEmpty()) {
        kind = detectUnrar(path, QStringLiteral("--version"));
    }

    if (!kind) {
        kind = detectUnrar(path, QStringLiteral("-v"));
    }

    if (!kind) {
        // no luck, print that
        qWarning() << "Neither unrar nor unarchiver were found.";
    } else {
        unrarPath = path;
        qCDebug(OkularComicbookDebug) << "detected:" << path << "(" << kind->name() << ")";
    }
}

UnrarHelper::~UnrarHelper()
{
    delete kind;
}

Unrar::Unrar()
    : QObject(nullptr)
    , mTempDir(nullptr)
{
}

Unrar::~Unrar()
{
    delete mTempDir;
}

bool Unrar::open(const QString &fileName)
{
    if (!isSuitableVersionAvailable()) {
        return false;
    }

    delete mTempDir;
    mTempDir = new QTemporaryDir();

    if (!mTempDir->isValid()) {
        return false;
    }
    mFileName = fileName;

    /**
     * Extract the archive to a temporary directory
     */
    mStdOutData.clear();
    mStdErrData.clear();

    const int ret = startSyncProcess(helper->kind->processOpenArchiveArgs(mFileName, mTempDir->path()));
    if (ret != 0) {
        delete mTempDir;
        mTempDir = nullptr;
        return false;
    }
    return true;
}

QStringList Unrar::list()
{
    mStdOutData.clear();
    mStdErrData.clear();

    if (!isSuitableVersionAvailable()) {
        return QStringList();
    }

    if (!mTempDir || startSyncProcess(helper->kind->processListArgs(mFileName)) != 0) {
        return {};
    }

    static const QRegularExpression regex(QStringLiteral("[\r\n]"));
    QStringList listFiles = helper->kind->processListing(QString::fromLocal8Bit(mStdOutData).split(regex, Qt::SkipEmptyParts));

    QString subDir;

    if (!listFiles.isEmpty() && listFiles.last().endsWith(QLatin1Char('/')) && helper->kind->name() == QLatin1String("unar")) {
        // Subfolder detected. The unarchiver is unable to extract all files into a single folder
        subDir = listFiles.last();
        listFiles.removeLast();
    }

    QStringList newList;
    for (const QString &f : std::as_const(listFiles)) {
        // Extract all the files to mTempDir regardless of their path inside the archive
        // This will break if ever an arvhice with two files with the same name in different subfolders
        QFileInfo fi(f);
        if (QFile::exists(mTempDir->path() + QLatin1Char('/') + subDir + fi.fileName())) {
            newList.append(subDir + fi.fileName());
        }
    }
    return newList;
}

QByteArray Unrar::contentOf(const QString &fileName) const
{
    if (!isSuitableVersionAvailable()) {
        return QByteArray();
    }

    QFile file(mTempDir->path() + QLatin1Char('/') + fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }

    return file.readAll();
}

QIODevice *Unrar::createDevice(const QString &fileName) const
{
    if (!isSuitableVersionAvailable()) {
        return nullptr;
    }

    std::unique_ptr<QFile> file(new QFile(mTempDir->path() + QLatin1Char('/') + fileName));
    if (!file->open(QIODevice::ReadOnly)) {
        return nullptr;
    }

    return file.release();
}

bool Unrar::isAvailable()
{
    return helper->kind;
}

bool Unrar::isSuitableVersionAvailable()
{
    if (!isAvailable()) {
        return false;
    }

    if (dynamic_cast<NonFreeUnrarFlavour *>(helper->kind) || dynamic_cast<UnarFlavour *>(helper->kind)) {
        return true;
    } else {
        return false;
    }
}

int Unrar::startSyncProcess(const ProcessArgs &args)
{
    QProcess process;
    process.setProgram(helper->kind->name() == QLatin1String("unar") && args.useLsar ? helper->lsarPath : helper->unrarPath);
    process.setArguments(args.appArgs);
    QElapsedTimer monitorClock;
    monitorClock.start();
    auto withinBudget = [&]() {
        if (!mTempDir) {
            return true;
        }
        // Do not rescan the output directory for every pipe chunk.
        if (monitorClock.elapsed() < 250 && process.state() != QProcess::NotRunning) {
            return true;
        }
        monitorClock.restart();
        qint64 bytes = 0;
        int entries = 0;
        const qint64 limit = Okular::expansionLimit(QFileInfo(mFileName).size());
        QDirIterator it(mTempDir->path(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo info = it.fileInfo();
            if (++entries > Okular::MaxArchiveEntries || info.isSymLink() || info.size() > limit - bytes) {
                return false;
            }
            if (info.isFile()) {
                bytes += info.size();
            }
        }
        const QStorageInfo storage(mTempDir->path());
        return !storage.isValid() || storage.bytesAvailable() > 16LL * 1024 * 1024;
    };
    return Okular::runBoundedHelper(process, mStdOutData, mStdErrData, Okular::MaxDocumentWorkMs, withinBudget);
}

#include "moc_unrar.cpp"
