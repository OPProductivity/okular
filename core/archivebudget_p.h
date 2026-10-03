/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OKULAR_ARCHIVEBUDGET_P_H
#define OKULAR_ARCHIVEBUDGET_P_H
#include "documentlimits_p.h"
#include <KArchiveDirectory>
#include <KArchiveFile>
#include <QBuffer>
#include <QElapsedTimer>
#include <QHash>
#include <QXmlStreamReader>
#include <memory>
namespace Okular
{
constexpr qint64 MaxArchivePartBytes = 64 * 1024 * 1024;
constexpr qint64 MaxArchiveTextBytes = 16 * 1024 * 1024;
inline QByteArray boundedArchiveData(const KArchiveFile *file, qint64 limit = MaxArchivePartBytes)
{
    if (!file || file->size() < 0 || file->size() > limit) {
        return {};
    }
    std::unique_ptr<QIODevice> input(file->createDevice());
    QByteArray data;
    QBuffer output(&data);
    output.open(QIODevice::WriteOnly);
    if (!input || !copyBoundedDocument(input.get(), &output, limit)) {
        return {};
    }
    return data;
}
// One budget/cache follows the archive through opening and later resource loads.
class ArchiveReadBudget
{
public:
    QByteArray read(const KArchiveFile *file)
    {
        if (!file || ++m_reads > 10000 || m_workMs >= 120000)
            return {};
        if (m_cache.contains(file))
            return m_cache.value(file);
        QElapsedTimer work;
        work.start();
        const QByteArray data = boundedArchiveData(file);
        m_workMs += work.elapsed();
        if (data.size() > 256 * 1024 * 1024 - m_bytes || m_workMs >= 120000)
            return {};
        m_bytes += data.size();
        m_cache.insert(file, data);
        return data;
    }

private:
    QHash<const KArchiveFile *, QByteArray> m_cache;
    qint64 m_bytes = 0;
    qint64 m_workMs = 0;
    int m_reads = 0;
};

inline bool boundedArchive(const KArchiveDirectory *root, qint64 compressedBytes)
{
    if (!root) {
        return false;
    }
    const qint64 limit = qMin<qint64>(256 * 1024 * 1024, expansionLimit(compressedBytes));
    qint64 total = 0;
    int count = 0;
    QElapsedTimer timer;
    timer.start();
    QList<QPair<const KArchiveDirectory *, int>> pending {{root, 0}};
    while (!pending.isEmpty()) {
        const auto [directory, depth] = pending.takeLast();
        if (depth > 128 || timer.elapsed() > 10000) {
            return false;
        }
        for (const QString &name : directory->entries()) {
            if (++count > 10000) {
                return false;
            }
            const KArchiveEntry *entry = directory->entry(name);
            if (!entry || !entry->symLinkTarget().isEmpty()) {
                return false;
            }
            if (entry->isDirectory()) {
                pending.append({static_cast<const KArchiveDirectory *>(entry), depth + 1});
            } else if (entry->isFile()) {
                const auto *file = static_cast<const KArchiveFile *>(entry);
                const QString suffix = name.section(QLatin1Char('.'), -1).toLower();
                const bool text = QStringList {QStringLiteral("xml"),
                                               QStringLiteral("rels"),
                                               QStringLiteral("fpage"),
                                               QStringLiteral("fdoc"),
                                               QStringLiteral("fdseq"),
                                               QStringLiteral("opf"),
                                               QStringLiteral("ncx"),
                                               QStringLiteral("html"),
                                               QStringLiteral("xhtml"),
                                               QStringLiteral("css")}
                                      .contains(suffix);
                const qint64 size = file->size();
                if (size < 0 || size > (text ? MaxArchiveTextBytes : MaxArchivePartBytes) || size > limit - total) {
                    return false;
                }
                const QByteArray verified = boundedArchiveData(file, text ? MaxArchiveTextBytes : MaxArchivePartBytes);
                if (verified.size() != size) {
                    return false;
                }
                if (text) {
                    QXmlStreamReader xml(verified);
                    int nesting = 0, elements = 0;
                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.isStartElement() && (++elements > 100000 || ++nesting > 128))
                            return false;
                        if (xml.isEndElement())
                            --nesting;
                        if (timer.elapsed() > 10000)
                            return false;
                    }
                }
                total += size;
            } else {
                return false;
            }
        }
    }
    return true;
}
}
#endif
