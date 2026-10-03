/*
    SPDX-FileCopyrightText: 2007 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "rendererthread.h"

#include "core/processbudget_p.h"
#include "core/rasterlimits_p.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "spectre_debug.h"

#include "core/generator.h"
#include "core/page.h"
#include "core/utils.h"

GSRendererThread *GSRendererThread::theRenderer = nullptr;

GSRendererThread *GSRendererThread::getCreateRenderer()
{
    if (!theRenderer) {
        theRenderer = new GSRendererThread();
    }
    return theRenderer;
}

GSRendererThread::GSRendererThread()
{
    m_renderContext = spectre_render_context_new();
}

GSRendererThread::~GSRendererThread()
{
    spectre_render_context_free(m_renderContext);
}

void GSRendererThread::addRequest(const GSRendererThreadRequest &req)
{
    m_queueMutex.lock();
    m_queue.enqueue(req);
    m_queueMutex.unlock();
    m_semaphore.release();
}

void GSRendererThread::run()
{
    while (true) {
        m_semaphore.acquire();
        {
            m_queueMutex.lock();
            GSRendererThreadRequest req = m_queue.dequeue();
            m_queueMutex.unlock();

            if (req.cancelled->load())
                continue;
            int width = req.width, height = req.height;
            if (req.orientation % 2)
                std::swap(width, height);
            QImage image;
            QTemporaryDir temporary;
            if (temporary.isValid() && Okular::boundedRasterSize(width, height)) {
                QProcess helper;
                const QString outputFile = temporary.filePath(QStringLiteral("page.png"));
                QString program = QCoreApplication::applicationDirPath() + QStringLiteral("/okular-spectre-render");
#ifdef Q_OS_WIN
                program += QStringLiteral(".exe");
#endif
                if (!QFileInfo::exists(program)) {
                    program = QStandardPaths::findExecutable(QStringLiteral("okular-spectre-render"));
                }
                helper.setProgram(program);
                helper.setArguments({req.fileName,
                                     QString::number(req.pageNumber),
                                     QString::number(req.magnify, 'g', 17),
                                     QString::number(req.platformFonts),
                                     QString::number(req.graphicsAAbits),
                                     QString::number(req.textAAbits),
                                     QString::number(width),
                                     QString::number(height),
                                     QString::number(req.orientation),
                                     outputFile});
                QByteArray out, err;
                if (Okular::runBoundedHelper(helper, out, err, 120000, [&]() { return !req.cancelled->load() && QFileInfo(outputFile).size() <= 256 * 1024 * 1024; }) == 0) {
                    image.load(outputFile);
                } else {
                    qCWarning(OkularSpectreDebug) << "PostScript rendering failed or exceeded its budget";
                }
            }
            if (!image.isNull() && (image.width() != req.width || image.height() != req.height)) {
                image = image.scaled(req.width, req.height);
            }
            if (req.cancelled->load())
                continue;
            QImage *imageResult = new QImage(image);
            Q_EMIT imageDone(req.owner, req.generation, imageResult, req.request);
        }
    }
}

#include "moc_rendererthread.cpp"
