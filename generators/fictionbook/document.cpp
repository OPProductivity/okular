/*
    SPDX-FileCopyrightText: 2007 Tobias Koenig <tokoe@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "document.h"

#include <QBuffer>
#include <QFile>
#include <QXmlStreamReader>
#include <core/documentlimits_p.h>
#include <memory>

#include <KLocalizedString>
#include <kzip.h>

using namespace FictionBook;

Document::Document(const QString &fileName)
    : mFileName(fileName)
{
}

bool Document::open()
{
    QIODevice *device;
    std::unique_ptr<QIODevice> archiveDevice;

    QFile file(mFileName);
    KZip zip(mFileName);
    if (mFileName.endsWith(QLatin1String(".fb")) || mFileName.endsWith(QLatin1String(".fb2"))) {
        if (!file.open(QIODevice::ReadOnly)) {
            setError(i18n("Unable to open document: %1", file.errorString()));
            return false;
        }

        device = &file;
    } else {
        if (!zip.open(QIODevice::ReadOnly)) {
            setError(i18n("Document is not a valid ZIP archive"));
            return false;
        }

        const KArchiveDirectory *directory = zip.directory();
        if (!directory) {
            setError(i18n("Invalid document structure (main directory is missing)"));
            return false;
        }

        const QStringList entries = directory->entries();

        QString documentFile;
        for (int i = 0; i < entries.count(); ++i) {
            const KArchiveEntry *candidate = directory->entry(entries[i]);
            if (candidate && candidate->isFile() && entries[i].endsWith(QLatin1String(".fb2"))) {
                documentFile = entries[i];
                break;
            }
        }

        if (documentFile.isEmpty()) {
            setError(i18n("No content found in the document"));
            return false;
        }

        const KArchiveFile *entry = static_cast<const KArchiveFile *>(directory->entry(documentFile));
        archiveDevice.reset(entry->createDevice());
        device = archiveDevice.get();
        if (!device) {
            setError(i18n("Unable to open document"));
            return false;
        }
    }

    QByteArray xml;
    QBuffer buffer(&xml);
    buffer.open(QIODevice::WriteOnly);
    if (!Okular::copyBoundedDocument(device, &buffer, 64LL * 1024 * 1024)) {
        setError(i18n("Document exceeds safe conversion limits"));
        return false;
    }
    // Check the XML before constructing a DOM or entering recursive converters.
    QXmlStreamReader reader(xml);
    int depth = 0;
    int nodes = 0;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (++depth > Okular::MaxDocumentNesting || ++nodes > 1000000) {
                setError(i18n("Document exceeds safe conversion limits"));
                return false;
            }
        } else if (reader.isEndElement()) {
            --depth;
        }
    }
    QString errorMsg;
    if (reader.hasError() || !mDocument.setContent(xml, true, &errorMsg)) {
        setError(i18n("Invalid XML document: %1", errorMsg));
        return false;
    }

    return true;
}

QDomDocument Document::content() const
{
    return mDocument;
}

QString Document::lastErrorString() const
{
    return mErrorString;
}

void Document::setError(const QString &error)
{
    mErrorString = error;
}
