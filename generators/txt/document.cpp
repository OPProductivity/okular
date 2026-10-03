/*
    SPDX-FileCopyrightText: 2013 Azat Khuzhin <a3at.mail@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "document.h"

#include <QDataStream>
#include <QFile>

#include <QDebug>
#include <QStringDecoder>

#include "debug_txt.h"

using namespace Txt;

Document::Document(const QString &fileName)
{
#ifdef TXT_DEBUG
    qCDebug(OkularTxtDebug) << "Opening file" << fileName;
#endif

    QFile plainFile(fileName);
    if (!plainFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCDebug(OkularTxtDebug) << "Can't open file" << plainFile.fileName();
        return;
    }

    // QTextDocument layout and undo storage multiply the input footprint.
    constexpr qint64 maxTextBytes = 16 * 1024 * 1024;
    if (plainFile.size() > maxTextBytes) {
        return;
    }
    const QByteArray buffer = plainFile.read(maxTextBytes + 1);
    if (buffer.size() > maxTextBytes || plainFile.error() != QFile::NoError) {
        return;
    }
    const QString text = toUnicode(buffer);
    if (text.count(QLatin1Char('\n')) > 100000) {
        return;
    }
    setUndoRedoEnabled(false);
    setPlainText(text);
    m_valid = true;
}

Document::~Document()
{
}

QString Document::toUnicode(const QByteArray &array)
{
    auto encoding = QStringConverter::encodingForHtml(array);
    QStringDecoder decoder {encoding.value_or(QStringConverter::Encoding::Utf8)};
    return decoder.decode(array);
}

Q_LOGGING_CATEGORY(OkularTxtDebug, "org.kde.okular.generators.txt", QtWarningMsg)

#include "moc_document.cpp"
