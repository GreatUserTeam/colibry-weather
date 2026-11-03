#include "ForecastCache.h"

#include "WeatherParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>

namespace colibry::weather {

// fileName is injected so tests can point the cache at a temporary path.
ForecastCache::ForecastCache(QString fileName)
    : m_file(std::move(fileName))
{
}

// Per-user cache location provided by Qt for this application.
QString ForecastCache::defaultFileName()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/forecast.json");
}

bool ForecastCache::save(const Place &place, const QByteArray &raw, const QDateTime &fetchedAtUtc)
{
    // Only cache answers the parser accepts: a damaged or unexpected payload
    // must not be replayed to the user later.
    if (!place.isValid() || !parseForecast(raw).ok) // never cache something that could not be shown
        return false;
    if (!QDir().mkpath(QFileInfo(m_file).absolutePath()))
        return false;

    QJsonObject p;
    p.insert(QStringLiteral("name"), place.name);
    p.insert(QStringLiteral("region"), place.region);
    p.insert(QStringLiteral("country"), place.country);
    p.insert(QStringLiteral("latitude"), place.latitude);
    p.insert(QStringLiteral("longitude"), place.longitude);
    QJsonObject root;
    root.insert(QStringLiteral("place"), p);
    // Timestamps are normalized to UTC and written in ISO form so the read
    // path can reconstruct them without any local-time guessing.
    root.insert(QStringLiteral("fetchedAt"), fetchedAtUtc.toUTC().toString(Qt::ISODate));
    root.insert(QStringLiteral("raw"), QString::fromUtf8(raw));

    // QSaveFile writes to a temporary file and atomically renames it into
    // place on commit(), so a crash mid-write never truncates the cache.
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

std::optional<Forecast> ForecastCache::load() const
{
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject root = doc.object();
    const QJsonObject p = root.value(QStringLiteral("place")).toObject();

    Place place;
    place.name = p.value(QStringLiteral("name")).toString();
    place.region = p.value(QStringLiteral("region")).toString();
    place.country = p.value(QStringLiteral("country")).toString();
    place.latitude = p.value(QStringLiteral("latitude")).toDouble(1000);
    place.longitude = p.value(QStringLiteral("longitude")).toDouble(1000);
    // A missing or corrupted place block makes the whole entry unusable.
    if (!place.isValid())
        return std::nullopt;

    // The original server answer is stored verbatim and re-parsed here;
    // any change in the cached payload that breaks parsing discards the entry.
    ForecastParse parsed = parseForecast(root.value(QStringLiteral("raw")).toString().toUtf8());
    if (!parsed.ok)
        return std::nullopt;
    parsed.forecast.place = place;
    parsed.forecast.fetchedAt = QDateTime::fromString(root.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate);
    if (!parsed.forecast.fetchedAt.isValid())
        return std::nullopt;
    // The timestamp was saved as UTC; keep the same interpretation on read.
    parsed.forecast.fetchedAt =
        QDateTime(parsed.forecast.fetchedAt.date(), parsed.forecast.fetchedAt.time(), QTimeZone::utc());
    return parsed.forecast;
}

void ForecastCache::clear()
{
    QFile::remove(m_file);
}

} // namespace colibry::weather
