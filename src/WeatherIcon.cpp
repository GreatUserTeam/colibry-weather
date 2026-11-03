#include "WeatherIcon.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace colibry::weather {

namespace {

const QColor kSun(0xF5, 0xB8, 0x1B);
const QColor kMoon(0xE8, 0xE4, 0xC9);
const QColor kCloudLight(0xE6, 0xEA, 0xEF);
const QColor kCloudDark(0x9A, 0xA5, 0xB1);
const QColor kRain(0x3B, 0x82, 0xF6);
const QColor kSnow(0xDB, 0xEA, 0xFE);
const QColor kBolt(0xFA, 0xCC, 0x15);

void drawSun(QPainter &p, const QPointF &c, qreal r)
{
    p.save();
    p.setPen(QPen(kSun, r * 0.14, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 8; ++i) {
        const qreal a = i * M_PI / 4;
        p.drawLine(c + QPointF(std::cos(a), std::sin(a)) * r * 1.3, c + QPointF(std::cos(a), std::sin(a)) * r * 1.7);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(kSun);
    p.drawEllipse(c, r, r);
    p.restore();
}

void drawMoon(QPainter &p, const QPointF &c, qreal r)
{
    p.save();
    QPainterPath moon;
    moon.addEllipse(c, r * 1.2, r * 1.2);
    QPainterPath bite;
    bite.addEllipse(c + QPointF(r * 0.6, -r * 0.4), r * 1.0, r * 1.0);
    p.setPen(Qt::NoPen);
    p.setBrush(kMoon);
    p.drawPath(moon.subtracted(bite));
    p.restore();
}

void drawCloud(QPainter &p, const QRectF &box, const QColor &color)
{
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const qreal w = box.width();
    const qreal h = box.height();
    QPainterPath cloud;
    cloud.setFillRule(Qt::WindingFill); // overlapping puffs must merge, not cancel out
    cloud.addEllipse(QRectF(box.left() + w * 0.05, box.top() + h * 0.40, w * 0.40, h * 0.60));
    cloud.addEllipse(QRectF(box.left() + w * 0.25, box.top() + h * 0.05, w * 0.50, h * 0.80));
    cloud.addEllipse(QRectF(box.left() + w * 0.55, box.top() + h * 0.30, w * 0.40, h * 0.70));
    cloud.addRoundedRect(QRectF(box.left() + w * 0.20, box.top() + h * 0.50, w * 0.60, h * 0.50), h * 0.25, h * 0.25);
    p.drawPath(cloud.simplified());
    p.restore();
}

void drawDrops(QPainter &p, const QRectF &area, int n, const QColor &color, bool flakes)
{
    p.save();
    p.setPen(QPen(color, area.width() * 0.04, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < n; ++i) {
        const qreal x = area.left() + area.width() * (i + 0.5) / n;
        const qreal y = area.top() + (i % 2) * area.height() * 0.2;
        if (flakes) {
            p.setBrush(color);
            p.drawEllipse(QPointF(x, y + area.height() * 0.3), area.width() * 0.035, area.width() * 0.035);
        } else {
            p.drawLine(QPointF(x, y), QPointF(x - area.width() * 0.04, y + area.height() * 0.5));
        }
    }
    p.restore();
}

void drawBolt(QPainter &p, const QRectF &area)
{
    p.save();
    QPainterPath bolt;
    const qreal x = area.left(), y = area.top(), w = area.width(), h = area.height();
    bolt.moveTo(x + w * 0.55, y);
    bolt.lineTo(x + w * 0.15, y + h * 0.60);
    bolt.lineTo(x + w * 0.48, y + h * 0.60);
    bolt.lineTo(x + w * 0.35, y + h);
    bolt.lineTo(x + w * 0.85, y + h * 0.35);
    bolt.lineTo(x + w * 0.52, y + h * 0.35);
    bolt.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(kBolt);
    p.drawPath(bolt);
    p.restore();
}

void drawFog(QPainter &p, const QRectF &box)
{
    p.save();
    p.setPen(QPen(kCloudDark, box.height() * 0.08, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 4; ++i) {
        const qreal y = box.top() + box.height() * (0.25 + i * 0.18);
        const qreal inset = (i % 2) * box.width() * 0.1;
        p.drawLine(QPointF(box.left() + inset, y), QPointF(box.right() - box.width() * 0.1 + inset, y));
    }
    p.restore();
}

} // namespace

QPixmap renderSky(Sky sky, bool day, int size, qreal dpr)
{
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    if (size <= 0)
        return pm;

    QPainter p(&pm);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const qreal s = size;
    const QPointF celestial(s * 0.34, s * 0.34);
    const qreal r = s * 0.17;
    const QRectF cloudBox(s * 0.12, s * 0.30, s * 0.78, s * 0.42);
    const QRectF below(s * 0.25, s * 0.68, s * 0.5, s * 0.25);

    auto body = [&](const QPointF &c, qreal radius) { day ? drawSun(p, c, radius) : drawMoon(p, c, radius); };

    switch (sky) {
    case Sky::Clear:
        body(QPointF(s * 0.5, s * 0.5), s * 0.22);
        break;
    case Sky::MostlyClear:
        body(celestial + QPointF(s * 0.1, s * 0.05), r);
        drawCloud(p, QRectF(s * 0.42, s * 0.50, s * 0.46, s * 0.30), kCloudLight);
        break;
    case Sky::PartlyCloudy:
        body(celestial, r);
        drawCloud(p, QRectF(s * 0.28, s * 0.38, s * 0.66, s * 0.42), kCloudLight);
        break;
    case Sky::Overcast:
        drawCloud(p, QRectF(s * 0.30, s * 0.16, s * 0.60, s * 0.38), kCloudDark);
        drawCloud(p, QRectF(s * 0.10, s * 0.32, s * 0.74, s * 0.46), kCloudLight);
        break;
    case Sky::Fog:
        drawCloud(p, QRectF(s * 0.14, s * 0.12, s * 0.72, s * 0.40), kCloudDark);
        drawFog(p, QRectF(s * 0.14, s * 0.52, s * 0.72, s * 0.40));
        break;
    case Sky::Drizzle:
        drawCloud(p, cloudBox, kCloudLight);
        drawDrops(p, below, 3, kRain, false);
        break;
    case Sky::Rain:
        drawCloud(p, cloudBox, kCloudDark);
        drawDrops(p, below, 4, kRain, false);
        break;
    case Sky::FreezingRain:
        drawCloud(p, cloudBox, kCloudDark);
        drawDrops(p, below, 2, kRain, false);
        drawDrops(p, below.translated(s * 0.1, 0), 2, kSnow, true);
        break;
    case Sky::Showers:
        body(celestial, r * 0.9);
        drawCloud(p, QRectF(s * 0.28, s * 0.34, s * 0.66, s * 0.38), kCloudDark);
        drawDrops(p, QRectF(s * 0.35, s * 0.70, s * 0.5, s * 0.24), 3, kRain, false);
        break;
    case Sky::Snow:
        drawCloud(p, cloudBox, kCloudLight);
        drawDrops(p, below, 4, kSnow, true);
        break;
    case Sky::SnowShowers:
        body(celestial, r * 0.9);
        drawCloud(p, QRectF(s * 0.28, s * 0.34, s * 0.66, s * 0.38), kCloudLight);
        drawDrops(p, QRectF(s * 0.35, s * 0.70, s * 0.5, s * 0.24), 3, kSnow, true);
        break;
    case Sky::Thunderstorm:
        drawCloud(p, QRectF(s * 0.12, s * 0.16, s * 0.78, s * 0.42), kCloudDark);
        drawBolt(p, QRectF(s * 0.38, s * 0.50, s * 0.30, s * 0.42));
        break;
    case Sky::Unknown:
        p.setPen(QPen(kCloudDark, s * 0.06, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(s * 0.2, s * 0.2, s * 0.6, s * 0.6));
        {
            QFont f;
            f.setPixelSize(static_cast<int>(s * 0.4));
            f.setBold(true);
            p.setFont(f);
            p.drawText(QRectF(0, 0, s, s), Qt::AlignCenter, QStringLiteral("?"));
        }
        break;
    }
    return pm;
}

} // namespace colibry::weather
