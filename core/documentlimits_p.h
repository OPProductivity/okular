/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef OKULAR_DOCUMENTLIMITS_P_H
#define OKULAR_DOCUMENTLIMITS_P_H

#include <QElapsedTimer>
#include <QFileDevice>
#include <QIODevice>
#include <QStorageInfo>

namespace Okular
{
// These apply to expansion, not ordinary uncompressed PDF loading.
inline constexpr qint64 MaxExpandedDocumentBytes = 1024LL * 1024 * 1024;
inline constexpr qint64 MaxDocumentMetadataBytes = 16LL * 1024 * 1024;
inline constexpr int MaxDocumentWorkMs = 120000;
inline constexpr int MaxArchiveEntries = 10000;
inline constexpr int MaxDocumentNesting = 128;

inline qint64 expansionLimit(qint64 compressedBytes)
{
    constexpr qint64 slack = 64LL * 1024 * 1024;
    return qMin(MaxExpandedDocumentBytes, qMax(slack, qMin(compressedBytes, MaxExpandedDocumentBytes) * 1000));
}

// Fail closed on read/write errors or exhausted budgets, and let the caller
// discard its temporary file. Keep the historical unbounded copy API intact.
inline bool copyBoundedDocument(QIODevice *from, QIODevice *to, qint64 limit)
{
    if (!from || !to || limit < 0) {
        return false;
    }
    if (auto *file = qobject_cast<QFileDevice *>(to)) {
        const QStorageInfo storage(file->fileName());
        if (storage.isValid() && storage.isReady()) {
            limit = qMin(limit, qMax(0LL, storage.bytesAvailable() - 16LL * 1024 * 1024));
        }
    }
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray buffer(65536, '\0');
    qint64 total = 0;
    while (elapsed.elapsed() < MaxDocumentWorkMs) {
        const qint64 read = from->read(buffer.data(), buffer.size());
        if (read < 0) {
            return false;
        }
        if (read == 0) {
            return from->atEnd();
        }
        if (read > limit - total || to->write(buffer.constData(), read) != read) {
            return false;
        }
        total += read;
    }
    return false;
}
}
#endif
