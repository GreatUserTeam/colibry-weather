#include "WeatherService.h"

#include "WeatherParser.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace colibry::weather {

WeatherService::WeatherService(QObject *parent, QNetworkAccessManager *manager)
    : QObject(parent)
    , m_manager(manager ? manager : new QNetworkAccessManager(this))
{
}

void WeatherService::setEndpoints(const QString &forecastUrl, const QString &geocodingUrl)
{
    m_forecastBase = forecastUrl;
    m_geocodingBase = geocodingUrl;
}

QNetworkReply *WeatherService::send(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(m_timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("colibry-weather/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/json");
    return m_manager->get(request);
}

QString WeatherService::checkReply(QNetworkReply *reply, QByteArray *body) const
{
    if (reply->isOpen()) {
        if (reply->bytesAvailable() > kMaxAnswerBytes)
            return tr("The weather service sent too much data.");
        *body = reply->readAll();
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        // The transfer timeout cancels the request (Qt reports it as "operation canceled" on some
        // versions). We never let our own cancellations reach this code, so both mean "too slow".
        if (reply->error() == QNetworkReply::TimeoutError || reply->error() == QNetworkReply::OperationCanceledError)
            return tr("The weather service did not answer in time.");
        // Open-Meteo explains errors in JSON ({"error":true,"reason":"..."}) even for 4xx.
        const QJsonObject o = QJsonDocument::fromJson(*body).object();
        const QString reason = o.value(QStringLiteral("reason")).toString();
        if (!reason.isEmpty())
            return reason;
        if (status >= 400)
            return tr("The weather service answered with error %1.").arg(status);
        return tr("Cannot reach the weather service. Check your internet connection.");
    }
    if (status < 200 || status >= 300)
        return tr("The weather service answered with error %1.").arg(status);
    return {};
}

void WeatherService::fetchForecast(const Place &place)
{
    if (!place.isValid()) {
        emit failed(QStringLiteral("forecast"), tr("This place has no valid location."));
        return;
    }
    if (m_forecastReply) {
        m_forecastReply->disconnect(this);
        m_forecastReply->abort();
        m_forecastReply->deleteLater();
    }

    QNetworkReply *reply = send(forecastUrl(m_forecastBase, place.latitude, place.longitude));
    m_forecastReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, place] {
        reply->deleteLater();
        if (m_forecastReply != reply)
            return; // replaced by a newer request
        m_forecastReply.clear();

        QByteArray body;
        const QString problem = checkReply(reply, &body);
        if (!problem.isEmpty()) {
            emit failed(QStringLiteral("forecast"), problem);
            return;
        }
        ForecastParse parsed = parseForecast(body);
        if (!parsed.ok) {
            emit failed(QStringLiteral("forecast"), parsed.error);
            return;
        }
        parsed.forecast.place = place;
        parsed.forecast.fetchedAt = QDateTime::currentDateTimeUtc();
        emit forecastReady(parsed.forecast, body);
    });
}

void WeatherService::searchPlaces(const QString &queryText)
{
    const QString query = queryText.trimmed();
    if (query.size() < 2) {
        emit failed(QStringLiteral("search"), tr("Type at least two letters."));
        return;
    }
    if (query.size() > 100) {
        emit failed(QStringLiteral("search"), tr("The search text is too long."));
        return;
    }
    if (m_searchReply) {
        m_searchReply->disconnect(this);
        m_searchReply->abort();
        m_searchReply->deleteLater();
    }

    QNetworkReply *reply = send(geocodingUrl(m_geocodingBase, query, m_language));
    m_searchReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, query] {
        reply->deleteLater();
        if (m_searchReply != reply)
            return;
        m_searchReply.clear();

        QByteArray body;
        const QString problem = checkReply(reply, &body);
        if (!problem.isEmpty()) {
            emit failed(QStringLiteral("search"), problem);
            return;
        }
        const PlacesParse parsed = parsePlaces(body);
        if (!parsed.ok) {
            emit failed(QStringLiteral("search"), parsed.error);
            return;
        }
        emit placesFound(query, parsed.places);
    });
}

void WeatherService::cancelAll()
{
    for (QPointer<QNetworkReply> *r : {&m_forecastReply, &m_searchReply}) {
        if (*r) {
            (*r)->disconnect(this);
            (*r)->abort();
            (*r)->deleteLater();
            r->clear();
        }
    }
}

} // namespace colibry::weather
