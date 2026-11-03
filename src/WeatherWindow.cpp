#include "WeatherWindow.h"

#include "WeatherIcon.h"
#include "core/WeatherParser.h"
#include "colibry/Keys.h"
#include "colibry/SettingsStore.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

namespace colibry::weather {

WeatherWindow::WeatherWindow(WeatherService &service, ForecastCache &cache, SettingsStore &settings, QWidget *parent)
    : QMainWindow(parent)
    , m_service(service)
    , m_cache(cache)
    , m_settings(settings)
{
    setWindowTitle(tr("Weather"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("weather-few-clouds")));
    resize(560, 700);
    setMinimumSize(420, 480);

    const Units fallback = QLocale().measurementSystem() == QLocale::MetricSystem ? Units::Metric : Units::Imperial;
    m_units = unitsFromString(m_settings.value(Keys::WeatherUnits).toString(), fallback);

    buildUi();

    const QByteArray geometry = m_settings.value(Keys::WeatherGeometry).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    connect(&m_service, &WeatherService::forecastReady, this, &WeatherWindow::onForecast);
    connect(&m_service, &WeatherService::placesFound, this, &WeatherWindow::onPlaces);
    connect(&m_service, &WeatherService::failed, this, &WeatherWindow::onFailed);

    m_auto = new QTimer(this);
    m_auto->setInterval(kAutoRefreshMs);
    connect(m_auto, &QTimer::timeout, this, [this] { refresh(); });
    m_auto->start();

    // Remembered place first, then the last stored forecast, then fresh data.
    const QString city = m_settings.value(Keys::WeatherCity).toString();
    Place saved;
    saved.name = city;
    saved.latitude = m_settings.value(Keys::WeatherLatitude).toDouble();
    saved.longitude = m_settings.value(Keys::WeatherLongitude).toDouble();
    if (saved.isValid() && !city.isEmpty())
        m_place = saved;

    if (const auto cached = m_cache.load()) {
        if (!m_place.isValid() || (qFuzzyCompare(cached->place.latitude + 1000, m_place.latitude + 1000)
                                   && qFuzzyCompare(cached->place.longitude + 1000, m_place.longitude + 1000))) {
            m_forecast = cached;
            m_place = cached->place;
            m_cached = true;
        }
    }
    render();
    if (m_place.isValid())
        m_service.fetchForecast(m_place);
}

void WeatherWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);

    auto *searchRow = new QHBoxLayout;
    m_search = new QLineEdit(central);
    m_search->setPlaceholderText(tr("Search for a city…"));
    m_search->setClearButtonEnabled(true);
    auto *go = new QPushButton(tr("Search"), central);
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(go);
    root->addLayout(searchRow);

    m_results = new QListWidget(central);
    m_results->setMaximumHeight(130);
    m_results->hide();
    root->addWidget(m_results);

    m_placeLabel = new QLabel(central);
    QFont pf = m_placeLabel->font();
    pf.setBold(true);
    pf.setPointSizeF(pf.pointSizeF() * 1.3);
    m_placeLabel->setFont(pf);
    m_placeLabel->setAlignment(Qt::AlignCenter);
    m_placeLabel->setWordWrap(true);
    root->addWidget(m_placeLabel);

    auto *now = new QHBoxLayout;
    now->addStretch();
    m_icon = new QLabel(central);
    m_icon->setFixedSize(96, 96);
    now->addWidget(m_icon);
    auto *col = new QVBoxLayout;
    m_temp = new QLabel(central);
    QFont tf = m_temp->font();
    tf.setPointSizeF(tf.pointSizeF() * 3.2);
    tf.setBold(true);
    m_temp->setFont(tf);
    m_condition = new QLabel(central);
    col->addWidget(m_temp);
    col->addWidget(m_condition);
    now->addLayout(col);
    now->addStretch();
    root->addLayout(now);

    m_details = new QLabel(central);
    m_details->setAlignment(Qt::AlignCenter);
    m_details->setWordWrap(true);
    root->addWidget(m_details);

    m_days = new QListWidget(central);
    m_days->setIconSize(QSize(36, 36));
    m_days->setAlternatingRowColors(true);
    m_days->setSelectionMode(QAbstractItemView::NoSelection);
    m_days->setFocusPolicy(Qt::NoFocus);
    root->addWidget(m_days, 1);

    auto *bottom = new QHBoxLayout;
    m_status = new QLabel(central);
    m_status->setWordWrap(true);
    m_unitsButton = new QPushButton(central);
    m_refreshButton = new QPushButton(tr("Refresh"), central);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_unitsButton);
    bottom->addWidget(m_refreshButton);
    root->addLayout(bottom);
    setCentralWidget(central);

    connect(go, &QPushButton::clicked, this, [this] { searchFor(m_search->text()); });
    connect(m_search, &QLineEdit::returnPressed, this, [this] { searchFor(m_search->text()); });
    connect(m_results, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { choosePlace(m_results->currentRow()); });
    connect(m_results, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { choosePlace(m_results->currentRow()); });
    connect(m_unitsButton, &QPushButton::clicked, this,
            [this] { setUnits(m_units == Units::Metric ? Units::Imperial : Units::Metric); });
    connect(m_refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
}

// ---------------------------------------------------------------------------

void WeatherWindow::searchFor(const QString &text)
{
    m_error.clear();
    m_searchNote = tr("Searching…");
    render();
    m_service.searchPlaces(text);
}

void WeatherWindow::onPlaces(const QString &, const QList<Place> &places)
{
    m_found = places;
    m_results->clear();
    for (const Place &p : places)
        m_results->addItem(p.label());
    m_results->setVisible(!places.isEmpty());
    m_searchNote = places.isEmpty() ? tr("No place found. Check the spelling.") : QString();
    if (!places.isEmpty())
        m_results->setCurrentRow(0);
    render();
}

bool WeatherWindow::choosePlace(int index)
{
    if (index < 0 || index >= m_found.size())
        return false;
    m_place = m_found.at(index);
    m_found.clear();
    m_results->clear();
    m_results->hide();
    m_searchNote.clear();
    m_search->clear();
    m_forecast.reset(); // the old data is for another place
    m_cached = false;
    m_error.clear();
    rememberPlace();
    render();
    m_service.fetchForecast(m_place);
    return true;
}

void WeatherWindow::refresh()
{
    if (!m_place.isValid()) {
        render();
        return;
    }
    m_service.fetchForecast(m_place);
}

void WeatherWindow::setUnits(Units units)
{
    m_units = units;
    m_settings.setValue(Keys::WeatherUnits, unitsToString(units));
    render();
}

void WeatherWindow::rememberPlace()
{
    m_settings.setValue(Keys::WeatherCity, m_place.label());
    m_settings.setValue(Keys::WeatherLatitude, m_place.latitude);
    m_settings.setValue(Keys::WeatherLongitude, m_place.longitude);
}

void WeatherWindow::onForecast(const Forecast &forecast, const QByteArray &raw)
{
    // An answer for a place that is no longer chosen is ignored.
    if (!qFuzzyCompare(forecast.place.latitude + 1000, m_place.latitude + 1000)
        || !qFuzzyCompare(forecast.place.longitude + 1000, m_place.longitude + 1000))
        return;
    m_forecast = forecast;
    m_cached = false;
    m_error.clear();
    m_cache.save(forecast.place, raw, forecast.fetchedAt);
    render();
}

void WeatherWindow::onFailed(const QString &what, const QString &message)
{
    if (what == QLatin1String("search")) {
        m_searchNote = message;
        m_results->hide();
        render();
        return;
    }
    // Forecast failed: keep showing what we have, but say that it is old.
    if (m_forecast)
        m_cached = true;
    setError(message);
}

void WeatherWindow::setError(const QString &message)
{
    m_error = message;
    render();
}

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

QString WeatherWindow::temperatureText() const
{
    return m_forecast ? formatTemperature(m_forecast->current.temperature, m_units) : QString();
}

QString WeatherWindow::conditionText() const
{
    return m_forecast ? describeCode(m_forecast->current.code) : QString();
}

QStringList WeatherWindow::detailLines() const
{
    QStringList lines;
    if (!m_forecast)
        return lines;
    const CurrentWeather &c = m_forecast->current;
    lines << tr("Feels like %1").arg(formatTemperature(c.apparent, m_units));
    lines << tr("Humidity %1").arg(formatPercent(c.humidity));
    QString wind = formatWind(c.windSpeed, m_units);
    if (c.windSpeed && c.windDirection)
        wind += QStringLiteral(" ") + compassPoint(*c.windDirection);
    lines << tr("Wind %1").arg(wind);
    lines << tr("Pressure %1").arg(formatPressure(c.pressure, m_units));
    return lines;
}

QStringList WeatherWindow::forecastLines() const
{
    QStringList lines;
    if (!m_forecast)
        return lines;
    const QLocale locale;
    for (const DayForecast &d : std::as_const(m_forecast->days)) {
        QString line = QStringLiteral("%1   %2   %3 / %4")
                           .arg(locale.toString(d.date, QStringLiteral("ddd d MMM")), describeCode(d.code),
                                formatTemperature(d.max, m_units), formatTemperature(d.min, m_units));
        if (d.precipitationProbability)
            line += tr("   rain %1").arg(formatPercent(d.precipitationProbability));
        lines << line;
    }
    return lines;
}

QString WeatherWindow::statusText() const
{
    if (!m_error.isEmpty()) {
        if (m_forecast)
            return tr("%1 Showing data from %2.")
                .arg(m_error, QLocale().toString(m_forecast->fetchedAt.toLocalTime(), QLocale::ShortFormat));
        return m_error;
    }
    if (!m_searchNote.isEmpty())
        return m_searchNote;
    if (!m_place.isValid())
        return tr("Search for your city to see the weather.");
    if (!m_forecast)
        return tr("Loading…");
    const QString when = QLocale().toString(m_forecast->fetchedAt.toLocalTime(), QLocale::ShortFormat);
    return m_cached ? tr("Saved data from %1 (refreshing…)").arg(when) : tr("Updated %1").arg(when);
}

void WeatherWindow::render()
{
    m_placeLabel->setText(m_place.isValid() ? m_place.label() : tr("No place chosen"));
    m_unitsButton->setText(m_units == Units::Metric ? tr("Switch to °F") : tr("Switch to °C"));

    m_days->clear();
    if (m_forecast) {
        const CurrentWeather &c = m_forecast->current;
        const bool day = c.isDay.value_or(true);
        m_icon->setPixmap(renderSky(skyForCode(c.code), day, 96, devicePixelRatioF()));
        m_temp->setText(temperatureText());
        m_condition->setText(conditionText());
        m_details->setText(detailLines().join(QStringLiteral("   ·   ")));

        const QStringList lines = forecastLines();
        for (qsizetype i = 0; i < lines.size(); ++i) {
            auto *item = new QListWidgetItem(lines.at(i), m_days);
            item->setIcon(QIcon(renderSky(skyForCode(m_forecast->days.at(i).code), true, 36, devicePixelRatioF())));
        }
    } else {
        m_icon->clear();
        m_temp->clear();
        m_condition->clear();
        m_details->clear();
    }
    m_status->setText(statusText());
}

void WeatherWindow::closeEvent(QCloseEvent *event)
{
    m_settings.setValue(Keys::WeatherGeometry, saveGeometry());
    QMainWindow::closeEvent(event);
}

} // namespace colibry::weather
