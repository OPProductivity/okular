/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QDir>
#include <QFileInfo>

namespace Okular
{
// Resolve only existing regular files inside the temporary extraction tree.
// Both lexical and canonical checks are needed: archive names can contain
// parent components, and extracted files can be symlinks.
inline QString safeExtractedFilePath(const QString &root, const QString &name)
{
    const QString relative = QDir::fromNativeSeparators(name);
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(QLatin1Char(':'))) {
        return {};
    }
    for (const QStringView component : QStringView(relative).split(QLatin1Char('/'))) {
        if (component.isEmpty() || component == QLatin1String(".") || component == QLatin1String("..")) {
            return {};
        }
    }
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    const QFileInfo candidate(QDir(root).filePath(relative));
    const QString canonicalFile = candidate.canonicalFilePath();
#ifdef Q_OS_WIN
    constexpr auto comparison = Qt::CaseInsensitive;
#else
    constexpr auto comparison = Qt::CaseSensitive;
#endif
    if (canonicalRoot.isEmpty() || canonicalFile.isEmpty() || !candidate.isFile() || candidate.isSymLink()
        || !canonicalFile.startsWith(canonicalRoot + QLatin1Char('/'), comparison)) {
        return {};
    }
    return canonicalFile;
}
}
