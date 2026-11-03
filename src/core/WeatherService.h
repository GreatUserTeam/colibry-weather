#pragma once

#include "WeatherTypes.h"

#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace colibry::weather {

/// Talks to Open-Meteo (forecast + geocoding). No account or key is needed.
///
/// Only one forecast request and one search are active at a time: a newer request replaces an
/// older one, and the older answer is never delivered. Every request has a time limit, answers
/// are limited in size, and every failure arrives as a readable message through failed().
class WeatherService : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultTimeoutMs = 10000;
    static constexpr qint64 kMaxAnswerBytes = 2 * 1024 * 1024;

    explicit WeatherService(QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr);

    void setEndpoints(const QString &forecastUrl, const QString &geocodingUrl);
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    void setLanguage(const QString &language) { m_language = language; }

    void fetchForecast(const Place &place);
    void searchPlaces(const QString &query);
    void cancelAll();
    bool isFetching() const { return !m_forecastReply.isNull(); }
    bool isSearching() const { return !m_searchReply.isNull(); }

signals:
    /// `raw` is the unchanged answer of the server, for the cache.
    void forecastReady(const colibry::weather::Forecast &forecast, const QByteArray &raw);
    void placesFound(const QString &query, const QList<colibry::weather::Place> &places);
    /// `what` is "forecast" or "search".
    void failed(const QString &what, const QString &message);

private:
    QNetworkReply *send(const QUrl &url);
    /// Returns an empty string when the reply is fine and its body was read into `body`.
    QString checkReply(QNetworkReply *reply, QByteArray *body) const;

    QNetworkAccessManager *m_manager;
    QString m_forecastBase = QStringLiteral("https://api.open-meteo.com/v1/forecast");
    QString m_geocodingBase = QStringLiteral("https://geocoding-api.open-meteo.com/v1/search");
    QString m_language = QStringLiteral("en");
    int m_timeoutMs = kDefaultTimeoutMs;
    QPointer<QNetworkReply> m_forecastReply;
    QPointer<QNetworkReply> m_searchReply;
};

} // namespace colibry::weather
