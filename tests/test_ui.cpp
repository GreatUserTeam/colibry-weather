#include "FakeHttpServer.h"
#include "WeatherIcon.h"
#include "WeatherWindow.h"
#include "colibry/Keys.h"
#include "colibry/SettingsStore.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace colibry;
using namespace colibry::weather;

class WeatherUiTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_tmp.isValid());
        QDir(m_tmp.path()).removeRecursively();
        QDir().mkpath(m_tmp.path());
        m_server = std::make_unique<FakeHttpServer>();
        m_server->setResponse(QStringLiteral("/v1/forecast"), {200, fixtures::forecast(21.4, 2)});
        m_server->setResponse(QStringLiteral("/v1/search"), {200, fixtures::places()});
        m_service = std::make_unique<WeatherService>();
        m_service->setEndpoints(m_server->url(QStringLiteral("/v1/forecast")), m_server->url(QStringLiteral("/v1/search")));
        m_cache = std::make_unique<ForecastCache>(m_tmp.path() + QStringLiteral("/cache/forecast.json"));
        m_settings = std::make_unique<SettingsStore>(m_tmp.path() + QStringLiteral("/s.ini"));
        m_settings->setValue(Keys::WeatherUnits, QStringLiteral("metric"));
    }

    void startsEmptyAndInvitesToSearch()
    {
        WeatherWindow w(*m_service, *m_cache, *m_settings);
        QVERIFY(!w.currentPlace().isValid());
        QVERIFY(!w.hasForecast());
        QVERIFY(w.statusText().contains(QStringLiteral("Search")));
        w.refresh(); // nothing chosen: must do nothing
        QCOMPARE(m_server->requestCount(), 0);
    }

    void searchThenChooseShowsWeatherAndRemembersPlace()
    {
        {
            WeatherWindow w(*m_service, *m_cache, *m_settings);
            w.searchFor(QStringLiteral("Paris"));
            QTRY_COMPARE(w.foundPlaces().size(), 2);
            QVERIFY(w.choosePlace(0));
            QTRY_VERIFY(w.hasForecast());
            QCOMPARE(w.temperatureText(), QStringLiteral("21°"));
            QCOMPARE(w.conditionText(), QStringLiteral("Partly cloudy"));
            QVERIFY(w.statusText().startsWith(QStringLiteral("Updated")));
            QVERIFY(!w.showsCachedData());
            QCOMPARE(w.forecastLines().size(), 3);
            QVERIFY(w.forecastLines().at(1).contains(QStringLiteral("17°")));
            QVERIFY(w.forecastLines().at(1).contains(QStringLiteral("80%")));
            QVERIFY(w.detailLines().join(QLatin1Char(' ')).contains(QStringLiteral("W")));
            QVERIFY(!w.choosePlace(5));
        }
        QCOMPARE(m_settings->value(Keys::WeatherCity).toString(), QStringLiteral("Paris, Île-de-France, France"));
        QVERIFY(qAbs(m_settings->value(Keys::WeatherLatitude).toDouble() - 48.85341) < 1e-6);
        QVERIFY(m_cache->load().has_value());
    }

    void reopeningShowsSavedDataImmediatelyThenRefreshes()
    {
        {
            WeatherWindow w(*m_service, *m_cache, *m_settings);
            w.searchFor(QStringLiteral("Paris"));
            QTRY_COMPARE(w.foundPlaces().size(), 2);
            w.choosePlace(0);
            QTRY_VERIFY(w.hasForecast());
        }
        m_server->setResponse(QStringLiteral("/v1/forecast"), {200, fixtures::forecast(30.0, 0), 300});
        WeatherWindow again(*m_service, *m_cache, *m_settings);
        QVERIFY(again.hasForecast());           // from disk, no waiting
        QVERIFY(again.showsCachedData());
        QCOMPARE(again.temperatureText(), QStringLiteral("21°"));
        QVERIFY(again.statusText().contains(QStringLiteral("Saved")));
        QTRY_COMPARE_WITH_TIMEOUT(again.temperatureText(), QStringLiteral("30°"), 3000);
        QVERIFY(!again.showsCachedData());
    }

    void offlineKeepsOldDataAndSaysSo()
    {
        {
            WeatherWindow w(*m_service, *m_cache, *m_settings);
            w.searchFor(QStringLiteral("Paris"));
            QTRY_COMPARE(w.foundPlaces().size(), 2);
            w.choosePlace(0);
            QTRY_VERIFY(w.hasForecast());
        }
        m_server->setResponse(QStringLiteral("/v1/forecast"), {500, "down"});
        WeatherWindow w(*m_service, *m_cache, *m_settings);
        QTRY_VERIFY(!w.errorText().isEmpty());
        QVERIFY(w.hasForecast());
        QVERIFY(w.showsCachedData());
        QCOMPARE(w.temperatureText(), QStringLiteral("21°"));
        QVERIFY(w.statusText().contains(QStringLiteral("Showing data from")));
        // network comes back: refresh clears the error
        m_server->setResponse(QStringLiteral("/v1/forecast"), {200, fixtures::forecast(25.0, 1)});
        w.refresh();
        QTRY_VERIFY(w.errorText().isEmpty());
        QCOMPARE(w.temperatureText(), QStringLiteral("25°"));
    }

    void firstRunOfflineShowsError()
    {
        m_server->setResponse(QStringLiteral("/v1/forecast"), {500, "down"});
        m_settings->setValue(Keys::WeatherCity, QStringLiteral("Paris"));
        m_settings->setValue(Keys::WeatherLatitude, 48.85);
        m_settings->setValue(Keys::WeatherLongitude, 2.35);
        WeatherWindow w(*m_service, *m_cache, *m_settings);
        QTRY_VERIFY(!w.errorText().isEmpty());
        QVERIFY(!w.hasForecast());
        QVERIFY(w.statusText().contains(QStringLiteral("500")));
    }

    void changingPlaceDropsOldDataAndIgnoresLateAnswers()
    {
        m_server->setResponse(QStringLiteral("/v1/forecast"), {200, fixtures::forecast(21.4, 2), 100});
        WeatherWindow w(*m_service, *m_cache, *m_settings);
        w.searchFor(QStringLiteral("Paris"));
        QTRY_COMPARE(w.foundPlaces().size(), 2);
        w.choosePlace(0);
        QTRY_VERIFY(w.hasForecast());
        w.searchFor(QStringLiteral("Paris"));
        QTRY_COMPARE(w.foundPlaces().size(), 2);
        w.choosePlace(1); // Texas
        QVERIFY(!w.hasForecast()); // not showing Paris data under the Texas name
        QVERIFY(w.currentPlace().label().contains(QStringLiteral("Texas")));
        QTRY_VERIFY(w.hasForecast());
    }

    void unitsSwitchRerendersAndPersists()
    {
        {
            WeatherWindow w(*m_service, *m_cache, *m_settings);
            w.searchFor(QStringLiteral("Paris"));
            QTRY_COMPARE(w.foundPlaces().size(), 2);
            w.choosePlace(0);
            QTRY_VERIFY(w.hasForecast());
            w.setUnits(Units::Imperial);
            QCOMPARE(w.temperatureText(), QStringLiteral("71°")); // 21.4 °C
            QVERIFY(w.detailLines().join(QLatin1Char(' ')).contains(QStringLiteral("mph")));
            QVERIFY(w.detailLines().join(QLatin1Char(' ')).contains(QStringLiteral("inHg")));
        }
        QCOMPARE(m_settings->value(Keys::WeatherUnits).toString(), QStringLiteral("imperial"));
        WeatherWindow again(*m_service, *m_cache, *m_settings);
        QCOMPARE(again.units(), Units::Imperial);
    }

    void searchProblemsAreShownNotThrown()
    {
        WeatherWindow w(*m_service, *m_cache, *m_settings);
        w.searchFor(QStringLiteral("x"));
        QVERIFY(w.statusText().contains(QStringLiteral("two letters")));
        m_server->setResponse(QStringLiteral("/v1/search"), {200, R"({})"});
        w.searchFor(QStringLiteral("zzzzzz"));
        QTRY_VERIFY(w.statusText().contains(QStringLiteral("No place found")));
        QVERIFY(w.foundPlaces().isEmpty());
        m_server->setResponse(QStringLiteral("/v1/search"), {500, "no"});
        w.searchFor(QStringLiteral("Paris"));
        QTRY_VERIFY(w.statusText().contains(QStringLiteral("500")));
    }

    void geometryIsRemembered()
    {
        {
            WeatherWindow w(*m_service, *m_cache, *m_settings);
            w.show();
            w.close();
        }
        QVERIFY(!m_settings->value(Keys::WeatherGeometry).toByteArray().isEmpty());
    }

    void iconsDrawSomethingForEverySkyAndSize()
    {
        const Sky all[] = {Sky::Unknown, Sky::Clear, Sky::MostlyClear, Sky::PartlyCloudy, Sky::Overcast, Sky::Fog,
                           Sky::Drizzle, Sky::Rain, Sky::FreezingRain, Sky::Snow, Sky::Showers, Sky::SnowShowers,
                           Sky::Thunderstorm};
        for (Sky sky : all) {
            for (bool day : {true, false}) {
                const QImage img = renderSky(sky, day, 64, 1.0).toImage();
                QCOMPARE(img.size(), QSize(64, 64));
                bool drawn = false;
                for (int y = 0; y < img.height() && !drawn; ++y)
                    for (int x = 0; x < img.width() && !drawn; ++x)
                        drawn = qAlpha(img.pixel(x, y)) > 0;
                QVERIFY2(drawn, qPrintable(QStringLiteral("sky %1 day %2").arg(static_cast<int>(sky)).arg(day)));
            }
        }
        QCOMPARE(renderSky(Sky::Rain, true, 32, 2.0).size(), QSize(64, 64));
        QVERIFY(renderSky(Sky::Rain, true, 0).isNull() || renderSky(Sky::Rain, true, 0).size().isEmpty());
    }

private:
    QTemporaryDir m_tmp;
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<WeatherService> m_service;
    std::unique_ptr<ForecastCache> m_cache;
    std::unique_ptr<SettingsStore> m_settings;
};

QTEST_MAIN(WeatherUiTest)
#include "test_ui.moc"
