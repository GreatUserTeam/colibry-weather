#include "WeatherParser.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#include <cmath>

namespace colibry::weather {

namespace {

// Central translation helper: every user-visible string in this file goes
// through it so the whole parser shares one translation context.
QString tr(const char *s)
{
    return QCoreApplication::translate("colibry::weather", s);
}

/// A number, or nothing when the value is missing, null, a string or not finite.
std::optional<double> number(const QJsonObject &o, const QString &key)
{
    const QJsonValue v = o.value(key);
    if (!v.isDouble())
        return std::nullopt;
    const double d = v.toDouble();
    if (!std::isfinite(d))
        return std::nullopt;
    return d;
}

/// Element i of an array as a finite number, or nothing when out of range or not numeric.
std::optional<double> numberAt(const QJsonArray &a, qsizetype i)
{
    if (i < 0 || i >= a.size() || !a.at(i).isDouble())
        return std::nullopt;
    const double d = a.at(i).toDouble();
    return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
}

/// Element i of an array as a QTime, parsed from an ISO date-time string; invalid when absent.
QTime timeFromIsoDateTime(const QJsonArray &a, qsizetype i)
{
    if (i < 0 || i >= a.size() || !a.at(i).isString())
        return {};
    const QDateTime dt = QDateTime::fromString(a.at(i).toString(), Qt::ISODate);
    return dt.isValid() ? dt.time() : QTime();
}

/// The service reports problems as {"error": true, "reason": "..."} instead of HTTP errors.
QString apiError(const QJsonObject &root)
{
    if (root.value(QStringLiteral("error")).toBool(false)) {
        const QString reason = root.value(QStringLiteral("reason")).toString();
        return reason.isEmpty() ? tr("The weather service reported an error.") : reason;
    }
    return {};
}

// Coordinates outside these ranges cannot describe a point on Earth.
bool validCoordinates(double lat, double lon)
{
    return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

} // namespace

QString unitsToString(Units u)
{
    return u == Units::Imperial ? QStringLiteral("imperial") : QStringLiteral("metric");
}

// Unknown stored values fall back to the passed default instead of failing.
Units unitsFromString(const QString &s, Units fallback)
{
    if (s == QLatin1String("imperial"))
        return Units::Imperial;
    if (s == QLatin1String("metric"))
        return Units::Metric;
    return fallback;
}

// A place is usable only when it has a name and sane coordinates.
bool Place::isValid() const
{
    return !name.trimmed().isEmpty() && validCoordinates(latitude, longitude);
}

// Display label: "Name, Region, Country", skipping the region when it
// duplicates the name and omitting empty parts.
QString Place::label() const
{
    QStringList parts{name};
    if (!region.isEmpty() && region != name)
        parts << region;
    if (!country.isEmpty())
        parts << country;
    return parts.join(QStringLiteral(", "));
}

// ---------------------------------------------------------------------------

/// Turns the forecast endpoint's JSON answer into a Forecast structure.
/// Optional fields stay empty when the service did not send them.
ForecastParse parseForecast(const QByteArray &json)
{
    ForecastParse result;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = tr("The weather service sent an answer that cannot be read.");
        return result;
    }
    const QJsonObject root = doc.object();
    if (const QString e = apiError(root); !e.isEmpty()) {
        result.error = e;
        return result;
    }

    const QJsonValue currentValue = root.value(QStringLiteral("current"));
    if (!currentValue.isObject()) {
        result.error = tr("The weather data is incomplete.");
        return result;
    }
    const QJsonObject cur = currentValue.toObject();
    CurrentWeather c;
    c.temperature = number(cur, QStringLiteral("temperature_2m"));
    if (!c.temperature) {
        result.error = tr("The weather data is incomplete.");
        return result;
    }
    c.apparent = number(cur, QStringLiteral("apparent_temperature"));
    c.humidity = number(cur, QStringLiteral("relative_humidity_2m"));
    c.windSpeed = number(cur, QStringLiteral("wind_speed_10m"));
    c.windDirection = number(cur, QStringLiteral("wind_direction_10m"));
    c.pressure = number(cur, QStringLiteral("surface_pressure"));
    if (const auto day = number(cur, QStringLiteral("is_day")))
        c.isDay = *day != 0.0;
    if (const auto code = number(cur, QStringLiteral("weather_code")))
        c.code = static_cast<int>(*code);
    // Qt::ISODate without an offset parses as local time already.
    c.time = QDateTime::fromString(cur.value(QStringLiteral("time")).toString(), Qt::ISODate);

    QList<DayForecast> days;
    const QJsonObject daily = root.value(QStringLiteral("daily")).toObject();
    // Every daily field is a parallel array; index i describes times[i].
    const QJsonArray times = daily.value(QStringLiteral("time")).toArray();
    const QJsonArray codes = daily.value(QStringLiteral("weather_code")).toArray();
    const QJsonArray maxes = daily.value(QStringLiteral("temperature_2m_max")).toArray();
    const QJsonArray mins = daily.value(QStringLiteral("temperature_2m_min")).toArray();
    const QJsonArray rain = daily.value(QStringLiteral("precipitation_probability_max")).toArray();
    const QJsonArray rises = daily.value(QStringLiteral("sunrise")).toArray();
    const QJsonArray sets = daily.value(QStringLiteral("sunset")).toArray();
    for (qsizetype i = 0; i < times.size(); ++i) {
        DayForecast d;
        d.date = QDate::fromString(times.at(i).toString(), Qt::ISODate);
        if (!d.date.isValid())
            continue;
        if (const auto code = numberAt(codes, i))
            d.code = static_cast<int>(*code);
        d.max = numberAt(maxes, i);
        d.min = numberAt(mins, i);
        d.precipitationProbability = numberAt(rain, i);
        d.sunrise = timeFromIsoDateTime(rises, i);
        d.sunset = timeFromIsoDateTime(sets, i);
        days.append(d);
    }

    result.forecast.current = c;
    result.forecast.days = days;
    result.ok = true;
    return result;
}

/// Turns the geocoding endpoint's JSON answer into a list of candidate places.
PlacesParse parsePlaces(const QByteArray &json)
{
    PlacesParse result;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = tr("The weather service sent an answer that cannot be read.");
        return result;
    }
    const QJsonObject root = doc.object();
    if (const QString e = apiError(root); !e.isEmpty()) {
        result.error = e;
        return result;
    }

    // No "results" key at all is how the service says "nothing found".
    const QJsonArray results = root.value(QStringLiteral("results")).toArray();
    for (const QJsonValue &v : results) {
        const QJsonObject o = v.toObject();
        Place p;
        p.name = o.value(QStringLiteral("name")).toString().trimmed();
        p.region = o.value(QStringLiteral("admin1")).toString().trimmed();
        p.country = o.value(QStringLiteral("country")).toString().trimmed();
        const auto lat = number(o, QStringLiteral("latitude"));
        const auto lon = number(o, QStringLiteral("longitude"));
        if (!lat || !lon)
            continue;
        p.latitude = *lat;
        p.longitude = *lon;
        if (p.isValid())
            result.places.append(p);
    }
    result.ok = true;
    return result;
}

/// Builds the forecast request URL for one point on the map.
QUrl forecastUrl(const QString &baseUrl, double latitude, double longitude)
{
    QUrl url(baseUrl);
    QUrlQuery q;
    // Four decimal places are roughly 10 meters; more would be noise.
    q.addQueryItem(QStringLiteral("latitude"), QString::number(latitude, 'f', 4));
    q.addQueryItem(QStringLiteral("longitude"), QString::number(longitude, 'f', 4));
    q.addQueryItem(QStringLiteral("current"),
                   QStringLiteral("temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,"
                                  "wind_speed_10m,wind_direction_10m,surface_pressure,is_day"));
    q.addQueryItem(QStringLiteral("daily"),
                   QStringLiteral("weather_code,temperature_2m_max,temperature_2m_min,"
                                  "precipitation_probability_max,sunrise,sunset"));
    // "auto" asks the service to tag times with the place's own time zone.
    q.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
    q.addQueryItem(QStringLiteral("forecast_days"), QStringLiteral("7"));
    url.setQuery(q);
    return url;
}

/// Builds the place search (geocoding) request URL.
QUrl geocodingUrl(const QString &baseUrl, const QString &query, const QString &language, int count)
{
    QUrl url(baseUrl);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), query.trimmed());
    q.addQueryItem(QStringLiteral("count"), QString::number(count));
    q.addQueryItem(QStringLiteral("language"), language);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);
    return url;
}

// ---------------------------------------------------------------------------

/// Maps the numeric WMO weather code to a coarse Sky category for icons.
Sky skyForCode(int code)
{
    switch (code) {
    case 0: return Sky::Clear;
    case 1: return Sky::MostlyClear;
    case 2: return Sky::PartlyCloudy;
    case 3: return Sky::Overcast;
    case 45: case 48: return Sky::Fog;
    case 51: case 53: case 55: return Sky::Drizzle;
    case 56: case 57: case 66: case 67: return Sky::FreezingRain;
    case 61: case 63: case 65: return Sky::Rain;
    case 71: case 73: case 75: case 77: return Sky::Snow;
    case 80: case 81: case 82: return Sky::Showers;
    case 85: case 86: return Sky::SnowShowers;
    case 95: case 96: case 99: return Sky::Thunderstorm;
    default: return Sky::Unknown;
    }
}

/// Human-readable text for a WMO weather code, translated and ready to show.
QString describeCode(int code)
{
    switch (code) {
    case 0: return tr("Clear sky");
    case 1: return tr("Mainly clear");
    case 2: return tr("Partly cloudy");
    case 3: return tr("Overcast");
    case 45: return tr("Fog");
    case 48: return tr("Freezing fog");
    case 51: return tr("Light drizzle");
    case 53: return tr("Drizzle");
    case 55: return tr("Heavy drizzle");
    case 56: case 57: return tr("Freezing drizzle");
    case 61: return tr("Light rain");
    case 63: return tr("Rain");
    case 65: return tr("Heavy rain");
    case 66: case 67: return tr("Freezing rain");
    case 71: return tr("Light snow");
    case 73: return tr("Snow");
    case 75: return tr("Heavy snow");
    case 77: return tr("Snow grains");
    case 80: return tr("Light showers");
    case 81: return tr("Showers");
    case 82: return tr("Heavy showers");
    case 85: case 86: return tr("Snow showers");
    case 95: return tr("Thunderstorm");
    case 96: case 99: return tr("Thunderstorm with hail");
    default: return tr("Unknown");
    }
}

/// Rounds and appends the degree sign; an em dash stands in for missing data.
QString formatTemperature(std::optional<double> celsius, Units units)
{
    if (!celsius)
        return QStringLiteral("—");
    const double v = units == Units::Imperial ? *celsius * 9.0 / 5.0 + 32.0 : *celsius;
    const int rounded = static_cast<int>(std::lround(v));
    return QStringLiteral("%1°").arg(rounded);
}

/// Wind speed in km/h, converted to mph for the imperial system.
QString formatWind(std::optional<double> kmh, Units units)
{
    if (!kmh)
        return QStringLiteral("—");
    if (units == Units::Imperial)
        return tr("%1 mph").arg(std::lround(*kmh * 0.621371));
    return tr("%1 km/h").arg(std::lround(*kmh));
}

/// Pressure in hPa, converted to inHg with two decimals for the imperial system.
QString formatPressure(std::optional<double> hpa, Units units)
{
    if (!hpa)
        return QStringLiteral("—");
    if (units == Units::Imperial)
        return tr("%1 inHg").arg(*hpa * 0.02953, 0, 'f', 2);
    return tr("%1 hPa").arg(std::lround(*hpa));
}

/// Whole percent value with a trailing percent sign.
QString formatPercent(std::optional<double> percent)
{
    if (!percent)
        return QStringLiteral("—");
    return QStringLiteral("%1%").arg(std::lround(*percent));
}

/// Converts degrees (0..360, negatives wrapped) to one of eight compass points.
QString compassPoint(double degrees)
{
    static const char *const points[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    double d = std::fmod(degrees, 360.0);
    if (d < 0)
        d += 360.0;
    const int index = static_cast<int>(std::floor((d + 22.5) / 45.0)) % 8;
    return QCoreApplication::translate("colibry::weather", points[index]);
}

} // namespace colibry::weather
