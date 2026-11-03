#include "FakeHttpServer.h"
#include "core/WeatherParser.h"
#include "core/WeatherService.h"

#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

using namespace colibry::weather;
Q_DECLARE_METATYPE(colibry::weather::Forecast)
Q_DECLARE_METATYPE(QList<colibry::weather::Place>)

namespace {
Place paris()
{
    Place p;
    p.name = QStringLiteral("Paris");
    p.latitude = 48.85;
    p.longitude = 2.35;
    return p;
}
} // namespace

class WeatherNetTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<Forecast>();
        qRegisterMetaType<QList<Place>>();
    }

    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_service = std::make_unique<WeatherService>();
        m_service->setEndpoints(m_server->url(QStringLiteral("/v1/forecast")), m_server->url(QStringLiteral("/v1/search")));
    }

    void fetchesAndParsesForecast()
    {
        m_server->setResponse(QStringLiteral("/v1/forecast"), {200, fixtures::forecast(7.2, 71)});
        QSignalSpy ready(m_service.get(), &WeatherService::forecastReady);
        QSignalSpy failed(m_service.get(), &WeatherService::failed);
        m_service->fetchForecast(paris());
        QVERIFY(m_service->isFetching());
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(failed.count(), 0);
        QVERIFY(!m_service->isFetching());
        const auto f = qvariant_cast<Forecast>(ready.first().at(0));
        QCOMPARE(*f.current.temperature, 7.2);
        QCOMPARE(f.current.code, 71);
        QCOMPARE(f.place.name, QStringLiteral("Paris"));
        QVERIFY(f.fetchedAt.isValid());
        QVERIFY(ready.first().at(1).toByteArray().contains("temperature_2m"));
        // the request really carried the coordinates
        QCOMPARE(m_server->requestCount(), 1);
        const QUrlQuery q(QUrl(m_server->requests().first()));
        QCOMPARE(q.queryItemValue(QStringLiteral("latitude")), QStringLiteral("48.8500"));
    }

    void searchesPlaces()
    {
        m_server->setResponse(QStringLiteral("/v1/search"), {200, fixtures::places()});
        QSignalSpy found(m_service.get(), &WeatherService::placesFound);
        m_service->searchPlaces(QStringLiteral("  Paris "));
        QTRY_COMPARE(found.count(), 1);
        QCOMPARE(found.first().at(0).toString(), QStringLiteral("Paris"));
        const auto places = qvariant_cast<QList<Place>>(found.first().at(1));
        QCOMPARE(places.size(), 2);
        QVERIFY(m_server->requests().first().contains(QStringLiteral("name=Paris")));
    }

    void searchWithNoMatchIsNotAnError()
    {
        m_server->setResponse(QStringLiteral("/v1/search"), {200, R"({"generationtime_ms":0.2})"});
        QSignalSpy found(m_service.get(), &WeatherService::placesFound);
        QSignalSpy failed(m_service.get(), &WeatherService::failed);
        m_service->searchPlaces(QStringLiteral("zzzzzzzz"));
        QTRY_COMPARE(found.count(), 1);
        QCOMPARE(failed.count(), 0);
        QVERIFY(qvariant_cast<QList<Place>>(found.first().at(1)).isEmpty());
    }

    void rejectsBadInputWithoutNetworkTraffic()
    {
        QSignalSpy failed(m_service.get(), &WeatherService::failed);
        m_service->searchPlaces(QStringLiteral("a"));
        m_service->searchPlaces(QStringLiteral("   "));
        m_service->searchPlaces(QString(101, QLatin1Char('x')));
        m_service->fetchForecast(Place());
        QCOMPARE(failed.count(), 4);
        QCOMPARE(m_server->requestCount(), 0);
    }

    void explainsServerErrors()
    {
        m_server->setResponse(QStringLiteral("/v1/forecast"), {400, R"({"error":true,"reason":"Latitude must be in range of -90 to 90°."})"});
        QSignalSpy failed(m_service.get(), &WeatherService::failed);
        m_service->fetchForecast(paris());
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("forecast"));
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("Latitude")));
    }

    void reportsHttp500AndGarbage()
    {
        QSignalSpy failed(m_service.get(), &WeatherService::failed);
        m_server->setResponse(QStringLiteral("/v1/forecast"), {500, "oops"});
        m_service->fetchForecast(paris());
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.last().at(1).toString().contains(QStringLiteral("500")));

        m_server->setResponse(QStringLiteral("/v1/forecast"), {200, "<html>captive portal</html>"});
        m_service->fetchForecast(paris());
        QTRY_COMPARE(failed.count(), 2);
        QVERIFY(!failed.last().at(1).toString().isEmpty());

        m_server->setResponse(QStringLiteral("/v1/forecast"), {200, R"({"current":{"temperature_2m":null}})"});
        m_service->fetchForecast(paris());
        QTRY_COMPARE(failed.count(), 3);
    }

    void timesOutWhenServerIsSilent()
    {
        m_service->setTimeoutMs(300);
        m_server->setResponse(QStringLiteral("/v1/forecast"), [] {
            FakeHttpServer::Response r;
            r.neverAnswer = true;
            return r;
        }());
        QSignalSpy failed(m_service.get(), &WeatherService::failed);
        QSignalSpy ready(m_service.get(), &WeatherService::forecastReady);
        m_service->fetchForecast(paris());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QCOMPARE(ready.count(), 0);
        QVERIFY2(failed.first().at(1).toString().contains(QStringLiteral("in time")), qPrintable(failed.first().at(1).toString()));
        QVERIFY(!m_service->isFetching());
    }

    void reportsUnreachableServer()
    {
        // a port nobody listens on
        m_service->setEndpoints(QStringLiteral("http://127.0.0.1:1/v1/forecast"), QStringLiteral("http://127.0.0.1:1/v1/search"));
        QSignalSpy failed(m_service.get(), &WeatherService::failed);
        m_service->fetchForecast(paris());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("internet")));
    }

    void droppedConnectionIsAnError()
    {
        m_server->setResponse(QStringLiteral("/v1/forecast"), [] {
            FakeHttpServer::Response r;
            r.dropConnection = true;
            return r;
        }());
        QSignalSpy failed(m_service.get(), &WeatherService::failed);
        m_service->fetchForecast(paris());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    }

    void newerRequestReplacesOlderOne()
    {
        FakeHttpServer::Response slow{200, fixtures::forecast(1.0), 400};
        m_server->setResponse(QStringLiteral("/v1/forecast"), slow);
        QSignalSpy ready(m_service.get(), &WeatherService::forecastReady);
        Place a = paris();
        a.name = QStringLiteral("First");
        Place b = paris();
        b.name = QStringLiteral("Second");
        m_service->fetchForecast(a);
        m_service->fetchForecast(b);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 3000);
        QTest::qWait(700); // the abandoned one must never arrive
        QCOMPARE(ready.count(), 1);
        QCOMPARE(qvariant_cast<Forecast>(ready.first().at(0)).place.name, QStringLiteral("Second"));
    }

    void cancelAllStopsEverything()
    {
        m_server->setResponse(QStringLiteral("/v1/forecast"), {200, fixtures::forecast(), 300});
        QSignalSpy ready(m_service.get(), &WeatherService::forecastReady);
        QSignalSpy failed(m_service.get(), &WeatherService::failed);
        m_service->fetchForecast(paris());
        m_service->cancelAll();
        QVERIFY(!m_service->isFetching());
        QTest::qWait(600);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(failed.count(), 0);
    }

    void searchAndForecastAreIndependent()
    {
        m_server->setResponse(QStringLiteral("/v1/forecast"), {200, fixtures::forecast(), 200});
        m_server->setResponse(QStringLiteral("/v1/search"), {200, fixtures::places()});
        QSignalSpy ready(m_service.get(), &WeatherService::forecastReady);
        QSignalSpy found(m_service.get(), &WeatherService::placesFound);
        m_service->fetchForecast(paris());
        m_service->searchPlaces(QStringLiteral("Paris"));
        QTRY_COMPARE(found.count(), 1);
        QTRY_COMPARE(ready.count(), 1);
    }

    void oversizedAnswerIsRefused()
    {
        QByteArray huge("{\"current\":{\"temperature_2m\":1},\"pad\":\"");
        huge += QByteArray(WeatherService::kMaxAnswerBytes + 1000, 'x');
        huge += "\"}";
        m_server->setResponse(QStringLiteral("/v1/forecast"), {200, huge});
        QSignalSpy failed(m_service.get(), &WeatherService::failed);
        QSignalSpy ready(m_service.get(), &WeatherService::forecastReady);
        m_service->fetchForecast(paris());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count() + ready.count(), 1, 5000);
        QCOMPARE(ready.count(), 0);
    }

private:
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<WeatherService> m_service;
};

QTEST_MAIN(WeatherNetTest)
#include "test_net.moc"
