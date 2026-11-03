#pragma once

#include "WeatherTypes.h"

#include <QByteArray>
#include <QUrl>

namespace colibry::weather {

struct ForecastParse
{
    bool ok = false;
    QString error;
    Forecast forecast; ///< place is not filled by the parser
};

struct PlacesParse
{
    bool ok = false;
    QString error;
    QList<Place> places;
};

/// Reads an Open-Meteo forecast answer. Missing optional values stay empty, a wrong answer
/// (not JSON, an API error, no current temperature) gives ok = false and a readable message.
ForecastParse parseForecast(const QByteArray &json);
/// Reads an Open-Meteo geocoding answer. "No match" is a success with an empty list.
PlacesParse parsePlaces(const QByteArray &json);

QUrl forecastUrl(const QString &baseUrl, double latitude, double longitude);
QUrl geocodingUrl(const QString &baseUrl, const QString &query, const QString &language, int count = 8);

// Weather codes
Sky skyForCode(int wmoCode);
QString describeCode(int wmoCode);

// Display helpers
QString formatTemperature(std::optional<double> celsius, Units units);
QString formatWind(std::optional<double> kmh, Units units);
QString formatPressure(std::optional<double> hpa, Units units);
QString formatPercent(std::optional<double> percent);
QString compassPoint(double degrees);

} // namespace colibry::weather
