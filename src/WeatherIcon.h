#pragma once

#include "core/WeatherTypes.h"

#include <QPixmap>

namespace colibry::weather {

/// Draws a weather symbol with plain QPainter shapes (no image files, any size, any theme).
QPixmap renderSky(Sky sky, bool day, int size, qreal devicePixelRatio = 1.0);

} // namespace colibry::weather
