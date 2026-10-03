/*
    SPDX-FileCopyrightText: 2008 Ely Levy <elylevy@cs.huji.ac.il>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "epubdocument.h"
#include "core/archivebudget_p.h"
#include "core/rasterlimits_p.h"
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include <QRegularExpression>

Q_LOGGING_CATEGORY(OkularEpuDebug, "org.kde.okular.generators.epu", QtWarningMsg)
using namespace Epub;

EpubDocument::EpubDocument(const QString &fileName, const QFont &font)
    : QTextDocument()
    , padding(20)
    , mFont(font)
{
    mArchive = std::make_unique<KZip>(fileName);
    if (!mArchive->open(QIODevice::ReadOnly) || !Okular::boundedArchive(mArchive->directory(), QFileInfo(fileName).size())) {
        return;
    }
    // libepub exposes chapter/resource URLs relative to the OPF package.
    const auto *containerEntry = mArchive->directory()->entry(QStringLiteral("META-INF/container.xml"));
    if (!containerEntry || !containerEntry->isFile()) {
        return;
    }
    QXmlStreamReader container(mReadBudget.read(static_cast<const KArchiveFile *>(containerEntry)));
    while (!container.atEnd()) {
        container.readNext();
        if (container.isStartElement() && container.name() == QLatin1String("rootfile")) {
            const QString package = container.attributes().value(QLatin1String("full-path")).toString();
            const int slash = package.lastIndexOf(QLatin1Char('/'));
            if (slash >= 0) {
                mPackageDirectory = package.left(slash + 1);
            }
            break;
        }
    }
    setUndoRedoEnabled(false);
#ifdef Q_OS_WIN
    mEpub = epub_open(qUtf8Printable(fileName), 2);
#else
    mEpub = epub_open(qPrintable(fileName), 2);
#endif

    setPageSize(QSizeF(600, 800));
    setDefaultStyleSheet(QStringLiteral("a { color: %1 }").arg(QColor(Qt::blue).name()));
}

bool EpubDocument::isValid()
{
    return (mEpub ? true : false);
}

EpubDocument::~EpubDocument()
{
    if (mEpub) {
        epub_close(mEpub);
    }

    epub_cleanup();
}

struct epub *EpubDocument::getEpub()
{
    return mEpub;
}

void EpubDocument::setCurrentSubDocument(const QString &doc)
{
    mCurrentSubDocument.clear();
    int index = doc.lastIndexOf(QLatin1Char('/'));
    if (index > 0) {
        mCurrentSubDocument = QUrl::fromLocalFile(doc.left(index + 1));
    }
}

int EpubDocument::maxContentHeight() const
{
    return pageSize().height() - (2 * padding);
}

int EpubDocument::maxContentWidth() const
{
    return pageSize().width() - (2 * padding);
}

QString EpubDocument::checkCSS(const QString &c)
{
    QString css = c;
    // remove paragraph line-heights
    static const QRegularExpression lineHeightRegex {QStringLiteral("line-height\\s*:\\s*[\\w\\.]*;")};
    css.remove(lineHeightRegex);

    // HACK transform em and rem notation to px, because QTextDocument doesn't support
    // em and rem.
    static const QRegularExpression cssSplitRegex {QStringLiteral("\\s+")};
    const QStringList cssArray = css.split(cssSplitRegex);
    QStringList cssArrayReplaced;
    std::size_t cssArrayCount = cssArray.count();
    std::size_t i = 0;
    static const QRegularExpression re(QStringLiteral("(([0-9]+)(\\.[0-9]+)?)r?em(.*)"));
    while (i < cssArrayCount) {
        const auto &item = cssArray[i];
        QRegularExpressionMatch match = re.match(item);
        if (match.hasMatch()) {
            double em = match.captured(1).toDouble();
            double px = em * mFont.pointSize();
            cssArrayReplaced.append(QStringLiteral("%1px%2").arg(px).arg(match.captured(4)));
        } else {
            cssArrayReplaced.append(item);
        }
        i++;
    }
    return cssArrayReplaced.join(QStringLiteral(" "));
}

QVariant EpubDocument::loadResource(int type, const QUrl &name)
{
    const QString fileInPath = mCurrentSubDocument.resolved(name).path();
    const QByteArray bytes = resourceData(fileInPath);
    const int size = bytes.size();
    if (bytes.isEmpty()) {
        return {};
    }

    if (type != QTextDocument::ImageResource) {
        // Text is UTF-16 after conversion; charge it before retaining a resource.
        const qint64 retained = (type == EpubDocument::AudioResource || type == EpubDocument::MovieResource) ? size : qint64(size) * 2;
        if (retained > 256 * 1024 * 1024 - mRetainedBytes)
            return {};
        mRetainedBytes += retained;
    }
    QVariant resource;

    switch (type) {
    case QTextDocument::ImageResource: {
        QBuffer buffer;
        buffer.setData(bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const QSize original = reader.size();
        if (!Okular::boundedRasterSize(original.width(), original.height())) {
            return {};
        }
        if (original.width() > maxContentWidth() || original.height() > maxContentHeight()) {
            reader.setScaledSize(original.scaled(maxContentWidth(), maxContentHeight(), Qt::KeepAspectRatio));
        }
        QImage img = reader.read();
        const int maxHeight = maxContentHeight();
        const int maxWidth = maxContentWidth();
        if (img.height() > maxHeight) {
            img = img.scaledToHeight(maxHeight, Qt::SmoothTransformation);
        }
        if (img.width() > maxWidth) {
            img = img.scaledToWidth(maxWidth, Qt::SmoothTransformation);
        }
        if (img.sizeInBytes() > 256 * 1024 * 1024 - mRetainedBytes)
            return {};
        mRetainedBytes += img.sizeInBytes();
        resource.setValue(img);
        break;
    }
    case QTextDocument::StyleSheetResource: {
        QString css = QString::fromUtf8(bytes);
        resource.setValue(checkCSS(css));
        break;
    }
    case EpubDocument::MovieResource: {
        resource.setValue(bytes);
        break;
    }
    case EpubDocument::AudioResource: {
        QByteArray ba(bytes);
        resource.setValue(ba);
        break;
    }
    default:
        resource.setValue(QString::fromUtf8(bytes));
        break;
    }


    // add to cache
    addResource(type, name, resource);

    return resource;
}

QByteArray EpubDocument::resourceData(const QString &path)
{
    QString decoded = QUrl::fromPercentEncoding(path.toUtf8()).section(QLatin1Char('#'), 0, 0);
    // QUrl::fromLocalFile/resolved supplies a leading slash for archive-relative URLs.
    while (decoded.startsWith(QLatin1Char('/'))) {
        decoded.remove(0, 1);
    }
    const QString local = QDir::cleanPath(mPackageDirectory + decoded);
    if (!mArchive || local.startsWith(QLatin1String("../")) || local.contains(QLatin1Char('\\'))) {
        return {};
    }
    const KArchiveEntry *entry = mArchive->directory()->entry(local);
    return entry && entry->isFile() ? mReadBudget.read(static_cast<const KArchiveFile *>(entry)) : QByteArray();
}
#include "moc_epubdocument.cpp"
