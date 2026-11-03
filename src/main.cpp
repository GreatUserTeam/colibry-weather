#include "WeatherWindow.h"
#include "colibry/SettingsStore.h"
#include "colibry/ThemeSync.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLocale>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("colibry-weather"));
    QApplication::setApplicationDisplayName(QStringLiteral("Weather"));
    QApplication::setApplicationVersion(QStringLiteral(Colibry_APPS_VERSION));
    QApplication::setOrganizationName(QStringLiteral("GreatUserTeam"));
    QApplication::setDesktopFileName(QStringLiteral("colibry-weather"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Weather for the Great User System (data: Open-Meteo.com)"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    colibry::SettingsStore settings;
    colibry::ThemeSync themeSync(app, settings);

    colibry::weather::WeatherService service;
    service.setLanguage(QLocale().name().section(QLatin1Char('_'), 0, 0));
    colibry::weather::ForecastCache cache(colibry::weather::ForecastCache::defaultFileName());
    colibry::weather::WeatherWindow window(service, cache, settings);
    window.show();
    return QApplication::exec();
}
