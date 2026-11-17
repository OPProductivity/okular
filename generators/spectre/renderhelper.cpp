/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core/rasterlimits_p.h"
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <libspectre/spectre.h>
#include <memory>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() != 11)
        return 2;
    const int pageNumber = args[2].toInt();
    const double scale = args[3].toDouble();
    const int width = args[7].toInt(), height = args[8].toInt();
    if (pageNumber < 0 || !std::isfinite(scale) || scale <= 0 || !Okular::boundedRasterSize(width, height))
        return 2;
    std::unique_ptr<SpectreDocument, decltype(&spectre_document_free)> doc(spectre_document_new(), spectre_document_free);
    if (!doc)
        return 4;
    spectre_document_load(doc.get(), QFile::encodeName(args[1]).constData());
    if (spectre_document_status(doc.get()) != SPECTRE_STATUS_SUCCESS || uint(pageNumber) >= spectre_document_get_n_pages(doc.get()))
        return 3;
    std::unique_ptr<SpectrePage, decltype(&spectre_page_free)> page(spectre_document_get_page(doc.get(), pageNumber), spectre_page_free);
    std::unique_ptr<SpectreRenderContext, decltype(&spectre_render_context_free)> context(spectre_render_context_new(), spectre_render_context_free);
    if (!page || !context)
        return 4;
    int pageWidth = 0, pageHeight = 0;
    spectre_page_get_size(page.get(), &pageWidth, &pageHeight);
    // libspectre rounds the page dimensions after applying the render scale.
    // The requested output dimensions can be different, so never wrap its
    // buffer using those requested dimensions.
    const double scaledWidth = pageWidth * scale + 0.5;
    const double scaledHeight = pageHeight * scale + 0.5;
    if (pageWidth <= 0 || pageHeight <= 0 || !std::isfinite(scaledWidth) || !std::isfinite(scaledHeight) || scaledWidth < 1 || scaledHeight < 1
        || scaledWidth > 32768 || scaledHeight > 32768)
        return 4;
    const int renderedWidth = int(scaledWidth);
    const int renderedHeight = int(scaledHeight);
    if (!Okular::boundedRasterSize(renderedWidth, renderedHeight))
        return 4;
    spectre_render_context_set_scale(context.get(), scale, scale);
    // Keep PostScript from changing Ghostscript's output buffer dimensions
    // after libspectre has computed them from the page bounding box.
    spectre_render_context_set_page_size(context.get(), pageWidth, pageHeight);
    spectre_render_context_set_use_platform_fonts(context.get(), args[4].toInt());
    spectre_render_context_set_antialias_bits(context.get(), args[5].toInt(), args[6].toInt());
    unsigned char *data = nullptr;
    int stride = 0;
    spectre_page_render(page.get(), context.get(), &data, &stride);
    std::unique_ptr<unsigned char, decltype(&free)> pixels(data, free);
    if (spectre_page_status(page.get()) != SPECTRE_STATUS_SUCCESS || !data || stride < renderedWidth * 4 || !Okular::boundedRasterSize(stride / 4, renderedHeight))
        return 4;
    QImage image(data, renderedWidth, renderedHeight, stride, QImage::Format_RGB32);
    image = image.copy(0, 0, std::min(width, renderedWidth), std::min(height, renderedHeight));
    if (image.isNull())
        return 4;
    for (int y = 0; y < image.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            row[x] |= 0xff000000;
    }
    const int orientation = args[9].toInt();
    if (orientation) {
        QTransform rotation;
        rotation.rotate(orientation * 90);
        image = image.transformed(rotation);
    }
    return image.save(args[10], "PNG") ? 0 : 5;
}
