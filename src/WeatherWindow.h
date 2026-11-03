#pragma once

#include "core/ForecastCache.h"
#include "core/WeatherService.h"
#include "core/WeatherTypes.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

namespace colibry {
class SettingsStore;
}

namespace colibry::weather {

/// Weather window: search for a place, see the current weather and 7 days ahead.
/// The last good answer is kept on disk and shown (and labeled as old) when the network fails.
class WeatherWindow : public QMainWindow
{
    Q_OBJECT

public:
    WeatherWindow(WeatherService &service, ForecastCache &cache, SettingsStore &settings, QWidget *parent = nullptr);

    // Actions (the widgets call these)
    void searchFor(const QString &text);
    bool choosePlace(int index);
    void refresh();
    void setUnits(Units units);

    // State, for tests and for the status line
    Place currentPlace() const { return m_place; }
    QList<Place> foundPlaces() const { return m_found; }
    Units units() const { return m_units; }
    bool showsCachedData() const { return m_cached; }
    bool hasForecast() const { return m_forecast.has_value(); }
    QString statusText() const;
    QString temperatureText() const;
    QString conditionText() const;
    QStringList detailLines() const;
    QStringList forecastLines() const;
    QString errorText() const { return m_error; }

    static constexpr int kAutoRefreshMs = 15 * 60 * 1000;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void onForecast(const Forecast &forecast, const QByteArray &raw);
    void onFailed(const QString &what, const QString &message);
    void onPlaces(const QString &query, const QList<Place> &places);
    void render();
    void setError(const QString &message);
    void rememberPlace();

    WeatherService &m_service;
    ForecastCache &m_cache;
    SettingsStore &m_settings;

    Place m_place;
    QList<Place> m_found;
    std::optional<Forecast> m_forecast;
    Units m_units = Units::Metric;
    bool m_cached = false;
    QString m_error;
    QString m_searchNote;

    QLineEdit *m_search = nullptr;
    QListWidget *m_results = nullptr;
    QLabel *m_placeLabel = nullptr;
    QLabel *m_icon = nullptr;
    QLabel *m_temp = nullptr;
    QLabel *m_condition = nullptr;
    QLabel *m_details = nullptr;
    QListWidget *m_days = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_unitsButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QTimer *m_auto = nullptr;
};

} // namespace colibry::weather
