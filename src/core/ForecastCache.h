#pragma once

#include "WeatherTypes.h"

#include <QByteArray>

namespace colibry::weather {

/// Keeps the last good forecast on disk, so that the app shows something when there is no network.
/// The server answer is stored unchanged and parsed again on load, so there is only one parser.
class ForecastCache
{
public:
    explicit ForecastCache(QString fileName);
    static QString defaultFileName();

    bool save(const Place &place, const QByteArray &raw, const QDateTime &fetchedAtUtc);
    /// Empty optional when there is no usable cache (missing, damaged, or for no valid place).
    std::optional<Forecast> load() const;
    void clear();

private:
    QString m_file;
};

} // namespace colibry::weather
