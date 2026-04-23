#ifndef WEATHERAPICLIENT_H
#define WEATHERAPICLIENT_H

/**
 * @file weatherapiclient.h
 * @brief Moduł pobierania danych pogodowych z Open-Meteo API.
 *
 * Klasa WeatherApiClient odpowiada za:
 * - zamianę nazwy miasta na współrzędne (geocoding),
 * - pobranie prognozy pogody (forecast),
 * - zapis danych do pliku CSV,
 * - pobranie danych o jakości powietrza (PM2.5) z Air Quality API.
 */

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

/**
 * @class WeatherApiClient
 * @brief Klient REST API Open-Meteo do pobierania danych pogodowych i AQI.
 *
 * Wykonuje trzy niezależne zapytania HTTP:
 * 1. Geocoding — zamiana nazwy miasta na latitude/longitude.
 * 2. Forecast — pobranie prognozy godzinowej temperatury, opadów itp.
 * 3. Air Quality — pobranie danych PM2.5 z air-quality-api.open-meteo.com.
 *
 * Zapytania 2 i 3 uruchamiane są równolegle po zakończeniu geocodingu.
 * Wyniki zapisywane są do pliku CSV (`weather_data.csv`).
 */
class WeatherApiClient : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Konstruktor klienta pogodowego.
     * @param parent Wskaźnik do obiektu nadrzędnego Qt.
     */
    explicit WeatherApiClient(QObject *parent = nullptr);

public slots:
    /**
     * @brief Pobiera dane pogodowe dla podanego miasta.
     * @param city Nazwa miasta (np. "Warsaw").
     * @param days Liczba dni prognozy (1-16, domyślnie 7).
     *
     * Uruchamia asynchroniczny pipeline:
     * geocoding → forecast + air-quality → sygnały dataReady + airQualityReady.
     */
    void fetchWeather(const QString &city, int days = 7);

public:
    /**
     * @brief Buduje URL do geocoding API.
     * @param city Nazwa miasta.
     * @return Pełny URL zapytania geocoding.
     */
    static QString buildGeocodingUrl(const QString &city);

    /**
     * @brief Buduje URL do forecast API.
     * @param latitude Szerokość geograficzna.
     * @param longitude Długość geograficzna.
     * @param days Liczba dni prognozy.
     * @return Pełny URL zapytania forecast.
     */
    static QString buildForecastUrl(double latitude, double longitude, int days);

    /**
     * @brief Buduje URL do Air Quality API Open-Meteo.
     * @param latitude Szerokość geograficzna.
     * @param longitude Długość geograficzna.
     * @return Pełny URL zapytania air-quality (PM2.5, 1 dzień prognozy).
     */
    static QString buildAirQualityUrl(double latitude, double longitude);

    /**
     * @brief Parsuje odpowiedź JSON z geocoding API.
     * @param data Surowe bajty odpowiedzi HTTP.
     * @param[out] lat Sparsowana szerokość geograficzna.
     * @param[out] lon Sparsowana długość geograficzna.
     * @param[out] resolvedName Nazwa miasta zwrócona przez API.
     * @return true jeśli parsowanie się powiodło.
     */
    bool parseGeocodingResponse(const QByteArray &data, double &lat, double &lon,
                                 QString &resolvedName);

    /**
     * @brief Parsuje odpowiedź JSON z forecast API i zapisuje do CSV.
     * @param data Surowe bajty odpowiedzi HTTP.
     * @param outputPath Ścieżka do pliku CSV.
     * @return true jeśli parsowanie i zapis się powiodły.
     */
    bool parseForecastResponse(const QByteArray &data, const QString &outputPath);

signals:
    /**
     * @brief Emitowany po pomyślnym pobraniu i zapisaniu danych prognozy.
     * @param csvPath Ścieżka do zapisanego pliku CSV.
     * @param cityName Rozpoznana nazwa miasta.
     */
    void dataReady(const QString &csvPath, const QString &cityName);

    /**
     * @brief Emitowany po pobraniu danych o jakości powietrza.
     *
     * Emitowany niezależnie od dataReady — może przyjść kilkaset ms później.
     * Jeśli dane są niedostępne, emitowany z wartością -1.0.
     *
     * @param pm25 Średnia wartość PM2.5 w µg/m³; -1.0 jeśli niedostępne.
     */
    void airQualityReady(double pm25);

    /**
     * @brief Emitowany w przypadku błędu (sieciowego lub parsowania).
     * @param message Opis błędu.
     */
    void errorOccurred(const QString &message);

private slots:
    /** @brief Obsługuje odpowiedź geocoding API. */
    void onGeocodingReply(QNetworkReply *reply);

    /** @brief Obsługuje odpowiedź forecast API. */
    void onForecastReply(QNetworkReply *reply);

    /** @brief Obsługuje odpowiedź Air Quality API. */
    void onAirQualityReply(QNetworkReply *reply);

private:
    /**
     * @brief Inicjuje asynchroniczne zapytanie o jakość powietrza.
     * @param latitude Szerokość geograficzna (z geocodingu).
     * @param longitude Długość geograficzna (z geocodingu).
     */
    void fetchAirQuality(double latitude, double longitude);

    QNetworkAccessManager *m_networkManager; ///< Menedżer połączeń sieciowych.
    QString m_city;              ///< Aktualne miasto zapytania.
    int     m_days;              ///< Liczba dni prognozy.
    double  m_latitude;          ///< Szerokość geograficzna miasta.
    double  m_longitude;         ///< Długość geograficzna miasta.
    QString m_csvPath;           ///< Ścieżka do pliku CSV z danymi.
    QString m_resolvedCityName;  ///< Nazwa miasta z API geocoding.
};

#endif // WEATHERAPICLIENT_H
