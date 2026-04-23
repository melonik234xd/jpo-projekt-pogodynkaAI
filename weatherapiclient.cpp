/**
 * @file weatherapiclient.cpp
 * @brief Implementacja klienta pobierającego dane pogodowe i PM2.5.
 *
 * Implementacja realizuje asynchroniczny przepływ:
 * geocoding miasta -> pobranie forecast -> pobranie air quality.
 */

#include "weatherapiclient.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QTextStream>
#include <QUrl>

/**
 * @brief Tworzy klienta i ustawia domyślną ścieżkę pliku CSV.
 * @param parent Obiekt nadrzędny Qt.
 */
WeatherApiClient::WeatherApiClient(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_days(7)
    , m_latitude(0.0)
    , m_longitude(0.0)
{
    m_csvPath = QCoreApplication::applicationDirPath() + "/weather_data.csv";
}

/**
 * @brief Rozpoczyna pobieranie danych pogodowych dla wskazanego miasta.
 * @param city Nazwa miasta podana przez użytkownika.
 * @param days Liczba dni prognozy (zakres 1-16).
 */
void WeatherApiClient::fetchWeather(const QString &city, int days)
{
    m_city = city.trimmed();
    m_days = qBound(1, days, 16);

    if (m_city.isEmpty()) {
        emit errorOccurred("Nazwa miasta nie może być pusta.");
        return;
    }

    QString url = buildGeocodingUrl(m_city);
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onGeocodingReply(reply);
    });
}

/**
 * @brief Buduje URL do endpointu geocoding Open-Meteo.
 * @param city Nazwa miasta.
 * @return Adres URL zapytania HTTP GET.
 */

QString WeatherApiClient::buildGeocodingUrl(const QString &city)
{
    return QString("https://geocoding-api.open-meteo.com/v1/search?name=%1&count=1&language=pl")
        .arg(city);
}

/**
 * @brief Buduje URL do endpointu forecast Open-Meteo.
 * @param latitude Szerokość geograficzna.
 * @param longitude Długość geograficzna.
 * @param days Liczba dni prognozy.
 * @return Adres URL zapytania HTTP GET.
 */
QString WeatherApiClient::buildForecastUrl(double latitude, double longitude, int days)
{
    return QString("https://api.open-meteo.com/v1/forecast?"
                   "latitude=%1&longitude=%2"
                   "&hourly=temperature_2m,precipitation,relative_humidity_2m,"
                   "wind_speed_10m,wind_direction_10m"
                   "&daily=sunrise,sunset"
                   "&forecast_days=%3"
                   "&timezone=auto")
        .arg(latitude,  0, 'f', 4)
        .arg(longitude, 0, 'f', 4)
        .arg(days);
}

/**
 * @brief Buduje URL do Open-Meteo Air Quality API.
 *
 * Pobiera dane PM2.5 dla 1 dnia prognozy.
 * Dokumentacja: https://air-quality-api.open-meteo.com
 *
 * @param latitude  Szerokość geograficzna.
 * @param longitude Długość geograficzna.
 * @return Pełny URL zapytania.
 */
QString WeatherApiClient::buildAirQualityUrl(double latitude, double longitude)
{
    return QString("https://air-quality-api.open-meteo.com/v1/air-quality?"
                   "latitude=%1&longitude=%2"
                   "&hourly=pm2_5"
                   "&forecast_days=1")
        .arg(latitude,  0, 'f', 4)
        .arg(longitude, 0, 'f', 4);
}

/**
 * @brief Parsuje odpowiedź geocoding i zwraca współrzędne pierwszego wyniku.
 * @param data Surowy JSON odpowiedzi.
 * @param[out] lat Szerokość geograficzna.
 * @param[out] lon Długość geograficzna.
 * @param[out] resolvedName Nazwa miasta zwrócona przez API.
 * @return true jeśli odpowiedź zawiera poprawny wynik geocoding.
 */

bool WeatherApiClient::parseGeocodingResponse(const QByteArray &data,
                                               double &lat, double &lon,
                                               QString &resolvedName)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonArray results = root.value("results").toArray();
    if (results.isEmpty()) return false;

    QJsonObject first = results.first().toObject();
    lat = first.value("latitude").toDouble();
    lon = first.value("longitude").toDouble();
    resolvedName = first.value("name").toString();

    return (lat != 0.0 || lon != 0.0);
}

/**
 * @brief Parsuje prognozę godzinową i zapisuje ją do pliku CSV.
 * @param data Surowy JSON odpowiedzi endpointu forecast.
 * @param outputPath Ścieżka docelowego pliku CSV.
 * @return true gdy parsowanie i zapis pliku zakończą się powodzeniem.
 */
bool WeatherApiClient::parseForecastResponse(const QByteArray &data, const QString &outputPath)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) return false;

    QJsonObject root   = doc.object();
    QJsonObject hourly = root.value("hourly").toObject();
    QJsonArray times   = hourly.value("time").toArray();
    QJsonArray temps   = hourly.value("temperature_2m").toArray();
    QJsonArray precip  = hourly.value("precipitation").toArray();
    QJsonArray humidity = hourly.value("relative_humidity_2m").toArray();
    QJsonArray wind    = hourly.value("wind_speed_10m").toArray();
    QJsonArray windDir = hourly.value("wind_direction_10m").toArray();

    if (times.isEmpty() || temps.isEmpty() || times.size() != temps.size()) return false;

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&file);
    out << "datetime,temperature,precipitation,humidity,wind_speed,wind_dir\n";
    for (int i = 0; i < times.size(); ++i) {
        double precipVal  = (i < precip.size())   ? precip[i].toDouble()   : 0.0;
        double humVal     = (i < humidity.size())  ? humidity[i].toDouble() : 0.0;
        double windVal    = (i < wind.size())      ? wind[i].toDouble()     : 0.0;
        double windDirVal = (i < windDir.size())   ? windDir[i].toDouble()  : 0.0;
        out << times[i].toString() << ","
            << temps[i].toDouble() << ","
            << precipVal << ","
            << humVal    << ","
            << windVal   << ","
            << windDirVal << "\n";
    }
    file.close();

    // Zapisz daily (wschód/zachód słońca) do JSON
    QJsonObject daily = root.value("daily").toObject();
    if (!daily.isEmpty()) {
        QString dailyPath = QCoreApplication::applicationDirPath() + "/weather_data_daily.json";
        QFile dailyFile(dailyPath);
        if (dailyFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            dailyFile.write(QJsonDocument(daily).toJson());
            dailyFile.close();
        }
    }
    return true;
}

/**
 * @brief Obsługuje odpowiedź geocoding i inicjuje pobranie forecast.
 * @param reply Odpowiedź HTTP zwrócona przez endpoint geocoding.
 */

void WeatherApiClient::onGeocodingReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (QFileInfo::exists(m_csvPath)) {
            emit errorOccurred(
                QString("Brak połączenia z internetem (%1). Użyto zbuforowanych danych.")
                    .arg(reply->errorString()));
            emit dataReady(m_csvPath, m_city);
        } else {
            emit errorOccurred(
                QString("Błąd sieci: %1. Brak zbuforowanych danych.").arg(reply->errorString()));
        }
        return;
    }

    QByteArray data = reply->readAll();
    double lat = 0.0, lon = 0.0;
    QString resolvedName;

    if (!parseGeocodingResponse(data, lat, lon, resolvedName)) {
        emit errorOccurred(
            QString("Nie znaleziono miasta: %1. Sprawdź pisownię.").arg(m_city));
        return;
    }

    m_latitude  = lat;
    m_longitude = lon;
    m_resolvedCityName = resolvedName;

    // Krok 2: Forecast
    QNetworkRequest request{QUrl(buildForecastUrl(m_latitude, m_longitude, m_days))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *forecastReply = m_networkManager->get(request);
    connect(forecastReply, &QNetworkReply::finished, this, [this, forecastReply]() {
        onForecastReply(forecastReply);
    });
}

/**
 * @brief Obsługuje odpowiedź forecast i uruchamia pobieranie PM2.5.
 * @param reply Odpowiedź HTTP zwrócona przez endpoint forecast.
 */

void WeatherApiClient::onForecastReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (QFileInfo::exists(m_csvPath)) {
            emit errorOccurred(
                QString("Brak połączenia (%1). Użyto zbuforowanych danych.")
                    .arg(reply->errorString()));
            emit dataReady(m_csvPath, m_resolvedCityName);
        } else {
            emit errorOccurred(
                QString("Błąd pobierania prognozy: %1").arg(reply->errorString()));
        }
        return;
    }

    QByteArray data = reply->readAll();
    if (!parseForecastResponse(data, m_csvPath)) {
        emit errorOccurred("Błąd parsowania danych pogodowych z API.");
        return;
    }

    // Emituj gotowość danych prognozy PRZED zapytaniem o AQI
    emit dataReady(m_csvPath, m_resolvedCityName);

    fetchAirQuality(m_latitude, m_longitude);
}

/**
 * @brief Inicjuje asynchroniczne zapytanie o dane PM2.5.
 *
 * Wywoływana automatycznie po pomyślnym zapisaniu prognozy.
 * Wynik dostarczany jest przez sygnał airQualityReady(pm25).
 *
 * @param latitude  Szerokość geograficzna.
 * @param longitude Długość geograficzna.
 */
void WeatherApiClient::fetchAirQuality(double latitude, double longitude)
{
    QNetworkRequest request{QUrl(buildAirQualityUrl(latitude, longitude))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAirQualityReply(reply);
    });
}

/**
 * @brief Obsługuje odpowiedź z Air Quality API i emituje airQualityReady.
 *
 * Parsuje pole hourly.pm2_5 z JSON, oblicza średnią z niepustych wartości
 * i emituje wynik. Jeśli wystąpi błąd, emituje airQualityReady(-1.0).
 *
 * @param reply Odpowiedź HTTP z air-quality-api.open-meteo.com.
 */
void WeatherApiClient::onAirQualityReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // Błąd sieci — nie przerywamy działania, tylko informujemy o braku AQI
        emit airQualityReady(-1.0);
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        emit airQualityReady(-1.0);
        return;
    }

    QJsonObject root   = doc.object();
    QJsonObject hourly = root.value("hourly").toObject();
    QJsonArray pm25arr = hourly.value("pm2_5").toArray();

    if (pm25arr.isEmpty()) {
        emit airQualityReady(-1.0);
        return;
    }

    // Oblicz średnią z niepustych (non-null) wartości PM2.5
    double sum = 0.0;
    int    cnt = 0;
    for (const QJsonValue &v : pm25arr) {
        if (!v.isNull() && v.isDouble()) {
            sum += v.toDouble();
            cnt++;
        }
    }

    if (cnt == 0) {
        emit airQualityReady(-1.0);
        return;
    }

    emit airQualityReady(sum / cnt);
}
