cmake_minimum_required(VERSION 3.21)

project(colibry-weather
    VERSION 0.2.0
    DESCRIPTION "Weather for the Colibry"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(Colibry_BUILD_TESTS "Build unit tests" ON)
option(Colibry_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(_qt_components Widgets Network)
if(Colibry_BUILD_TESTS)
    list(APPEND _qt_components Test)
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS ${_qt_components})

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(ColibryApps)

if(Colibry_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(common)
add_library(gus_weather_core STATIC
    src/core/WeatherTypes.h
    src/core/WeatherParser.h
    src/core/WeatherParser.cpp
    src/core/WeatherService.h
    src/core/WeatherService.cpp
    src/core/ForecastCache.h
    src/core/ForecastCache.cpp
)
target_include_directories(gus_weather_core PUBLIC src)
target_link_libraries(gus_weather_core PUBLIC Qt6::Widgets Qt6::Network)
colibry_target_defaults(gus_weather_core)

add_library(gus_weather_ui STATIC
    src/WeatherIcon.h
    src/WeatherIcon.cpp
    src/WeatherWindow.h
    src/WeatherWindow.cpp
)
target_include_directories(gus_weather_ui PUBLIC src)
target_link_libraries(gus_weather_ui PUBLIC gus_weather_core colibry_common Qt6::Widgets Qt6::Network)
colibry_target_defaults(gus_weather_ui)

add_executable(colibry-weather src/main.cpp)
target_link_libraries(colibry-weather PRIVATE gus_weather_ui)
colibry_target_defaults(colibry-weather)

colibry_install_app(colibry-weather
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/colibry-weather.desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/colibry-weather.svg)

if(Colibry_BUILD_TESTS)
    colibry_add_test(weather_core SOURCES tests/test_core.cpp LINK gus_weather_core)
    colibry_add_test(weather_net SOURCES tests/test_net.cpp tests/FakeHttpServer.h LINK gus_weather_core)
    colibry_add_test(weather_ui SOURCES tests/test_ui.cpp tests/FakeHttpServer.h LINK gus_weather_ui)
endif()
