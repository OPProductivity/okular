/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core/rasterlimits_p.h"
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTransform>
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
    spectre_document_load(doc.get(), QFile::encodeName(args[1]).constData());
    if (spectre_document_status(doc.get()) != SPECTRE_STATUS_SUCCESS || uint(pageNumber) >= spectre_document_get_n_pages(doc.get()))
        return 3;
    std::unique_ptr<SpectrePage, decltype(&spectre_page_free)> page(spectre_document_get_page(doc.get(), pageNumber), spectre_page_free);
    std::unique_ptr<SpectreRenderContext, decltype(&spectre_render_context_free)> context(spectre_render_context_new(), spectre_render_context_free);
    spectre_render_context_set_scale(context.get(), scale, scale);
    spectre_render_context_set_use_platform_fonts(context.get(), args[4].toInt());
    spectre_render_context_set_antialias_bits(context.get(), args[5].toInt(), args[6].toInt());
    unsigned char *data = nullptr;
    int stride = 0;
    spectre_page_render(page.get(), context.get(), &data, &stride);
    std::unique_ptr<unsigned char, decltype(&free)> pixels(data, free);
    if (!data || stride < width * 4 || !Okular::boundedRasterSize(stride / 4, height))
        return 4;
    QImage image(data, width, height, stride, QImage::Format_RGB32);
    image = image.copy();
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
