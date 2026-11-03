#pragma once

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

/// A tiny HTTP server on 127.0.0.1 for tests. Answers are chosen by request path.
class FakeHttpServer : public QObject
{
    Q_OBJECT

public:
    struct Response
    {
        int status = 200;
        QByteArray body;
        int delayMs = 0;       ///< wait before answering
        bool neverAnswer = false; ///< accept the connection and say nothing (for timeouts)
        bool dropConnection = false; ///< close at once without an answer
    };

    explicit FakeHttpServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        const bool ok = m_server.listen(QHostAddress::LocalHost, 0);
        Q_ASSERT(ok);
        Q_UNUSED(ok);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection())
                handle(socket);
        });
    }

    QString url(const QString &path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path);
    }

    void setResponse(const QString &path, const Response &r) { m_responses.insert(path, r); }
    QStringList requests() const { return m_requests; }
    int requestCount() const { return static_cast<int>(m_requests.size()); }
    void clearRequests() { m_requests.clear(); }

private:
    void handle(QTcpSocket *socket)
    {
        auto *buffer = new QByteArray;
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            if (!buffer->contains("\r\n\r\n"))
                return;
            const QByteArray line = buffer->left(buffer->indexOf("\r\n"));
            const QList<QByteArray> parts = line.split(' ');
            const QString target = parts.size() > 1 ? QString::fromUtf8(parts.at(1)) : QString();
            m_requests << target;
            buffer->clear();
            disconnect(socket, &QTcpSocket::readyRead, this, nullptr);

            const QString path = QUrl(target).path();
            const Response r = m_responses.value(path, Response{404, R"({"error":true,"reason":"not found"})", 0, false, false});
            if (r.neverAnswer)
                return;
            if (r.dropConnection) {
                socket->abort();
                return;
            }
            auto reply = [socket, r] {
                if (socket->state() != QAbstractSocket::ConnectedState)
                    return;
                QByteArray out = "HTTP/1.1 " + QByteArray::number(r.status) + " X\r\n";
                out += "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(r.body.size());
                out += "\r\nConnection: close\r\n\r\n";
                out += r.body;
                socket->write(out);
                socket->disconnectFromHost();
            };
            if (r.delayMs > 0)
                QTimer::singleShot(r.delayMs, socket, reply);
            else
                reply();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QObject::destroyed, this, [buffer] { delete buffer; });
    }

    QTcpServer m_server;
    QHash<QString, Response> m_responses;
    QStringList m_requests;
};

namespace fixtures {

inline QByteArray forecast(double temp = 21.4, int code = 2)
{
    return QByteArray(R"({
      "latitude": 48.86, "longitude": 2.35, "timezone": "Europe/Paris", "utc_offset_seconds": 3600,
      "current": {"time": "2026-03-10T14:00", "interval": 900,
        "temperature_2m": )") + QByteArray::number(temp) + R"(, "apparent_temperature": 19.8,
        "relative_humidity_2m": 55, "weather_code": )" + QByteArray::number(code) + R"(,
        "wind_speed_10m": 12.6, "wind_direction_10m": 250, "surface_pressure": 1008.4, "is_day": 1},
      "daily": {
        "time": ["2026-03-10","2026-03-11","2026-03-12"],
        "weather_code": [2, 61, 95],
        "temperature_2m_max": [22.1, 17.0, 15.5],
        "temperature_2m_min": [9.0, 8.2, null],
        "precipitation_probability_max": [10, 80, 90],
        "sunrise": ["2026-03-10T07:01","2026-03-11T06:59","2026-03-12T06:57"],
        "sunset": ["2026-03-10T18:40","2026-03-11T18:42","2026-03-12T18:43"]}
    })";
}

inline QByteArray places()
{
    return R"({"results":[
      {"id":2988507,"name":"Paris","latitude":48.85341,"longitude":2.3488,"country":"France","admin1":"Île-de-France","country_code":"FR"},
      {"id":4717560,"name":"Paris","latitude":33.66094,"longitude":-95.55551,"country":"United States","admin1":"Texas"}
    ],"generationtime_ms":0.5})";
}

} // namespace fixtures
