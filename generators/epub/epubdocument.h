/*
    SPDX-FileCopyrightText: 2008 Ely Levy <elylevy@cs.huji.ac.il>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef EPUB_DOCUMENT_H
#define EPUB_DOCUMENT_H

#include "core/archivebudget_p.h"
#include <KZip>
#include <QImage>
#include <QLoggingCategory>
#include <QTextDocument>
#include <QUrl>
#include <QVariant>
#include <epub.h>
#include <memory>

namespace Epub
{
class EpubDocument : public QTextDocument
{
    Q_OBJECT

public:
    explicit EpubDocument(const QString &fileName, const QFont &font);
    ~EpubDocument() override;
    bool isValid();
    QByteArray resourceData(const QString &path);
    struct epub *getEpub();
    void setCurrentSubDocument(const QString &doc);
    int maxContentHeight() const;
    int maxContentWidth() const;
    enum Multimedia { MovieResource = QTextDocument::UserResource, AudioResource };

protected:
    QVariant loadResource(int type, const QUrl &name) override;

private:
    QString checkCSS(const QString &css);

    struct epub *mEpub = nullptr;
    std::unique_ptr<KZip> mArchive;
    Okular::ArchiveReadBudget mReadBudget;
    qint64 mRetainedBytes = 0;
    QString mPackageDirectory;
    QUrl mCurrentSubDocument;

    int padding;
    QFont mFont;

    friend class Converter;
};

}
Q_DECLARE_LOGGING_CATEGORY(OkularEpuDebug)
#endif
