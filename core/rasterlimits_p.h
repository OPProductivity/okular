/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OKULAR_RASTERLIMITS_P_H
#define OKULAR_RASTERLIMITS_P_H
#include <QtGlobal>
namespace Okular
{
inline bool boundedRasterSize(quint64 width, quint64 height)
{
    return width > 0 && height > 0 && width <= 32768 && height <= 32768 && width * height <= 64ULL * 1024 * 1024;
}
}
#endif
