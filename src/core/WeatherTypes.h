#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTime>

#include <optional>

namespace colibry::weather {

enum class Units { Metric, Imperial };

QString unitsToString(Units u);
Units unitsFromString(const QString &s, Units fallback);

struct Place
{
    QString name;
    QString region;  ///< state / province, may be empty
    QString country; ///< may be empty
    double latitude = 0;
    double longitude = 0;

    bool isValid() const;
    /// "Paris, Île-de-France, France"
    QString label() const;
};

struct CurrentWeather
{
    QDateTime time; ///< local time at the place (no zone attached)
    std::optional<double> temperature;      ///< °C
    std::optional<double> apparent;         ///< °C
    std::optional<double> humidity;         ///< %
    std::optional<double> windSpeed;        ///< km/h
    std::optional<double> windDirection;    ///< degrees
    std::optional<double> pressure;         ///< hPa
    std::optional<bool> isDay;
    int code = -1;                          ///< WMO weather code, -1 = unknown
};

struct DayForecast
{
    QDate date;
    int code = -1;
    std::optional<double> max;              ///< °C
    std::optional<double> min;              ///< °C
    std::optional<double> precipitationProbability; ///< %
    QTime sunrise;
    QTime sunset;
};

struct Forecast
{
    Place place;
    CurrentWeather current;
    QList<DayForecast> days;
    QDateTime fetchedAt; ///< UTC
};

/// What a weather code looks like, independent of any drawing code.
enum class Sky {
    Unknown, Clear, MostlyClear, PartlyCloudy, Overcast, Fog, Drizzle, Rain, FreezingRain, Snow, Showers, SnowShowers, Thunderstorm
};

} // namespace colibry::weather
