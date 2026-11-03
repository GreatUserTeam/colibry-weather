#include "core/ForecastCache.h"
#include "core/WeatherParser.h"
#include "FakeHttpServer.h"

#include <QFile>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

using namespace colibry::weather;

class WeatherCoreTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesForecast()
    {
        const ForecastParse r = parseForecast(fixtures::forecast());
        QVERIFY2(r.ok, qPrintable(r.error));
        const CurrentWeather &c = r.forecast.current;
        QCOMPARE(*c.temperature, 21.4);
        QCOMPARE(*c.apparent, 19.8);
        QCOMPARE(*c.humidity, 55.0);
        QCOMPARE(*c.windSpeed, 12.6);
        QCOMPARE(*c.windDirection, 250.0);
        QCOMPARE(*c.pressure, 1008.4);
        QCOMPARE(c.isDay.value(), true);
        QCOMPARE(c.code, 2);
        QCOMPARE(c.time.date(), QDate(2026, 3, 10));
        QCOMPARE(c.time.time(), QTime(14, 0));

        QCOMPARE(r.forecast.days.size(), 3);
        const DayForecast &d = r.forecast.days.at(1);
        QCOMPARE(d.date, QDate(2026, 3, 11));
        QCOMPARE(d.code, 61);
        QCOMPARE(*d.max, 17.0);
        QCOMPARE(*d.min, 8.2);
        QCOMPARE(*d.precipitationProbability, 80.0);
        QCOMPARE(d.sunrise, QTime(6, 59));
        QCOMPARE(d.sunset, QTime(18, 42));
        QVERIFY(!r.forecast.days.at(2).min.has_value()); // null stays empty
    }

    void missingOptionalPartsAreFine()
    {
        const ForecastParse r = parseForecast(R"({"current":{"temperature_2m": 3}})");
        QVERIFY(r.ok);
        QVERIFY(!r.forecast.current.humidity);
        QVERIFY(!r.forecast.current.isDay);
        QCOMPARE(r.forecast.current.code, -1);
        QVERIFY(!r.forecast.current.time.isValid());
        QVERIFY(r.forecast.days.isEmpty());
    }

    void rejectsWrongAnswers()
    {
        const QList<QByteArray> bad = {
            "", "not json", "[]", "42", "{}", R"({"current": 5})", R"({"current": {}})",
            R"({"current": {"temperature_2m": "warm"}})", R"({"current": {"temperature_2m": null}})",
            R"({"error": true, "reason": "Latitude must be in range of -90 to 90°."})",
        };
        for (const QByteArray &b : bad) {
            const ForecastParse r = parseForecast(b);
            QVERIFY2(!r.ok, b.constData());
            QVERIFY2(!r.error.isEmpty(), b.constData());
        }
        QVERIFY(parseForecast(R"({"error": true, "reason": "Latitude must be in range of -90 to 90°."})").error.contains(QStringLiteral("Latitude")));
    }

    void tolerantToOddDailyData()
    {
        const ForecastParse r = parseForecast(R"({
          "current": {"temperature_2m": 1.5},
          "daily": {"time": ["2026-01-01", "garbage", "2026-01-03", 7],
                    "weather_code": [1, 2],
                    "temperature_2m_max": [5, "x", 7, 8],
                    "sunrise": ["bad", null, "2026-01-03T08:00"]}})");
        QVERIFY(r.ok);
        QCOMPARE(r.forecast.days.size(), 2); // only the two valid dates
        QCOMPARE(r.forecast.days.at(0).code, 1);
        QCOMPARE(*r.forecast.days.at(0).max, 5.0);
        QVERIFY(!r.forecast.days.at(0).sunrise.isValid());
        QCOMPARE(r.forecast.days.at(1).code, -1); // array too short
        QCOMPARE(*r.forecast.days.at(1).max, 7.0);
        QCOMPARE(r.forecast.days.at(1).sunrise, QTime(8, 0));
    }

    void parsesPlaces()
    {
        const PlacesParse r = parsePlaces(fixtures::places());
        QVERIFY(r.ok);
        QCOMPARE(r.places.size(), 2);
        QCOMPARE(r.places.at(0).name, QStringLiteral("Paris"));
        QCOMPARE(r.places.at(0).region, QStringLiteral("Île-de-France"));
        QCOMPARE(r.places.at(0).label(), QStringLiteral("Paris, Île-de-France, France"));
        QCOMPARE(r.places.at(1).longitude, -95.55551);
    }

    void noResultsIsSuccessWithEmptyList()
    {
        const PlacesParse r = parsePlaces(R"({"generationtime_ms":0.3})");
        QVERIFY(r.ok);
        QVERIFY(r.places.isEmpty());
        QVERIFY(parsePlaces(R"({"results":[]})").ok);
    }

    void skipsBrokenPlaces()
    {
        const PlacesParse r = parsePlaces(R"({"results":[
          {"name":"Good","latitude":10,"longitude":20},
          {"name":"","latitude":10,"longitude":20},
          {"name":"NoLat","longitude":20},
          {"name":"Far","latitude":95,"longitude":20},
          {"name":"Far2","latitude":10,"longitude":-181},
          {"name":"Str","latitude":"10","longitude":20},
          5, null,
          {"name":"  Padded  ","latitude":-33.9,"longitude":151.2,"country":"Australia"}]})");
        QVERIFY(r.ok);
        QCOMPARE(r.places.size(), 2);
        QCOMPARE(r.places.at(1).name, QStringLiteral("Padded"));
        QVERIFY(!parsePlaces("garbage").ok);
        QVERIFY(!parsePlaces(R"({"error":true,"reason":"Bad"})").ok);
    }

    void placeLabelAvoidsRepeats()
    {
        Place p;
        p.name = QStringLiteral("Singapore");
        p.region = QStringLiteral("Singapore");
        p.country = QStringLiteral("Singapore");
        p.latitude = 1.3;
        p.longitude = 103.8;
        QCOMPARE(p.label(), QStringLiteral("Singapore, Singapore"));
        p.country.clear();
        QCOMPARE(p.label(), QStringLiteral("Singapore"));
    }

    void buildsUrls()
    {
        const QUrl f = forecastUrl(QStringLiteral("https://x.test/v1/forecast"), 48.8566, -2.5);
        QCOMPARE(f.host(), QStringLiteral("x.test"));
        const QUrlQuery q(f);
        QCOMPARE(q.queryItemValue(QStringLiteral("latitude")), QStringLiteral("48.8566"));
        QCOMPARE(q.queryItemValue(QStringLiteral("longitude")), QStringLiteral("-2.5000"));
        QVERIFY(q.queryItemValue(QStringLiteral("current")).contains(QStringLiteral("temperature_2m")));
        QVERIFY(q.queryItemValue(QStringLiteral("daily")).contains(QStringLiteral("sunrise")));
        QCOMPARE(q.queryItemValue(QStringLiteral("timezone")), QStringLiteral("auto"));

        // A name with spaces, "&" and non-Latin letters must survive as one value.
        const QUrl g = geocodingUrl(QStringLiteral("https://x.test/v1/search"), QStringLiteral("  Санкт-Петербург & Co  "), QStringLiteral("ru"));
        const QUrlQuery gq(g);
        QCOMPARE(gq.queryItemValue(QStringLiteral("name"), QUrl::FullyDecoded), QStringLiteral("Санкт-Петербург & Co"));
        QCOMPARE(gq.queryItemValue(QStringLiteral("language")), QStringLiteral("ru"));
        QVERIFY(!g.toEncoded().contains(' ')); // spaces must be percent-encoded on the wire
    }

    void weatherCodes()
    {
        QCOMPARE(skyForCode(0), Sky::Clear);
        QCOMPARE(skyForCode(2), Sky::PartlyCloudy);
        QCOMPARE(skyForCode(63), Sky::Rain);
        QCOMPARE(skyForCode(75), Sky::Snow);
        QCOMPARE(skyForCode(96), Sky::Thunderstorm);
        QCOMPARE(skyForCode(-1), Sky::Unknown);
        QCOMPARE(skyForCode(1234), Sky::Unknown);
        QCOMPARE(describeCode(0), QStringLiteral("Clear sky"));
        QCOMPARE(describeCode(999), QStringLiteral("Unknown"));
        // every code the service documents has a description and a sky
        for (int code : {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99}) {
            QVERIFY2(skyForCode(code) != Sky::Unknown, qPrintable(QString::number(code)));
            QVERIFY2(describeCode(code) != QStringLiteral("Unknown"), qPrintable(QString::number(code)));
        }
    }

    void formatsUnits()
    {
        QCOMPARE(formatTemperature(21.4, Units::Metric), QStringLiteral("21°"));
        QCOMPARE(formatTemperature(21.5, Units::Metric), QStringLiteral("22°"));
        QCOMPARE(formatTemperature(-0.4, Units::Metric), QStringLiteral("0°")); // never "-0"
        QCOMPARE(formatTemperature(-5.0, Units::Metric), QStringLiteral("-5°"));
        QCOMPARE(formatTemperature(0.0, Units::Imperial), QStringLiteral("32°"));
        QCOMPARE(formatTemperature(100.0, Units::Imperial), QStringLiteral("212°"));
        QCOMPARE(formatTemperature(-40.0, Units::Imperial), QStringLiteral("-40°"));
        QCOMPARE(formatTemperature(std::nullopt, Units::Metric), QStringLiteral("—"));
        QCOMPARE(formatWind(10.0, Units::Metric), QStringLiteral("10 km/h"));
        QCOMPARE(formatWind(16.0934, Units::Imperial), QStringLiteral("10 mph"));
        QCOMPARE(formatPressure(1013.25, Units::Metric), QStringLiteral("1013 hPa"));
        QCOMPARE(formatPressure(1013.25, Units::Imperial), QStringLiteral("29.92 inHg"));
        QCOMPARE(formatPercent(54.6), QStringLiteral("55%"));
        QCOMPARE(formatPercent(std::nullopt), QStringLiteral("—"));
        QCOMPARE(unitsFromString(QStringLiteral("imperial"), Units::Metric), Units::Imperial);
        QCOMPARE(unitsFromString(QStringLiteral("???"), Units::Imperial), Units::Imperial);
        QCOMPARE(unitsFromString(unitsToString(Units::Metric), Units::Imperial), Units::Metric);
    }

    void compassPoints()
    {
        QCOMPARE(compassPoint(0), QStringLiteral("N"));
        QCOMPARE(compassPoint(22.4), QStringLiteral("N"));
        QCOMPARE(compassPoint(22.6), QStringLiteral("NE"));
        QCOMPARE(compassPoint(90), QStringLiteral("E"));
        QCOMPARE(compassPoint(250), QStringLiteral("W"));
        QCOMPARE(compassPoint(359), QStringLiteral("N"));
        QCOMPARE(compassPoint(360), QStringLiteral("N"));
        QCOMPARE(compassPoint(-90), QStringLiteral("W"));
        QCOMPARE(compassPoint(725), QStringLiteral("N"));
    }

    void cacheRoundTrip()
    {
        QTemporaryDir tmp;
        ForecastCache cache(tmp.path() + QStringLiteral("/sub/forecast.json"));
        QVERIFY(!cache.load().has_value());
        Place p;
        p.name = QStringLiteral("Paris");
        p.country = QStringLiteral("France");
        p.latitude = 48.85;
        p.longitude = 2.35;
        const QDateTime when(QDate(2026, 3, 10), QTime(13, 0), QTimeZone::utc());
        QVERIFY(cache.save(p, fixtures::forecast(), when));
        const auto loaded = cache.load();
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->place.name, QStringLiteral("Paris"));
        QCOMPARE(loaded->place.latitude, 48.85);
        QCOMPARE(loaded->fetchedAt, when);
        QCOMPARE(*loaded->current.temperature, 21.4);
        QCOMPARE(loaded->days.size(), 3);
        cache.clear();
        QVERIFY(!cache.load().has_value());
    }

    void cacheRefusesUnusableData()
    {
        QTemporaryDir tmp;
        ForecastCache cache(tmp.path() + QStringLiteral("/f.json"));
        Place p;
        p.name = QStringLiteral("X");
        p.latitude = 1;
        p.longitude = 1;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(!cache.save(p, "garbage", now));
        QVERIFY(!cache.save(Place(), fixtures::forecast(), now));
        QVERIFY(!QFileInfo::exists(tmp.path() + QStringLiteral("/f.json")));
    }

    void damagedCacheIsIgnored()
    {
        QTemporaryDir tmp;
        const QString file = tmp.path() + QStringLiteral("/f.json");
        for (const char *bad : {"", "nope", "[]", "{}", R"({"place":{},"raw":"x"})",
                                R"({"place":{"name":"A","latitude":1,"longitude":1},"fetchedAt":"x","raw":"{}"})"}) {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bad);
            f.close();
            ForecastCache cache(file);
            QVERIFY2(!cache.load().has_value(), bad);
        }
    }
};

QTEST_MAIN(WeatherCoreTest)
#include "test_core.moc"
