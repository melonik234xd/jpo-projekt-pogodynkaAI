/**
 * @file test_weatherparser.cpp
 * @brief Testy jednostkowe modułu parsowania danych pogodowych.
 */

#include <QTest>
#include <QTemporaryFile>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "../weatherapiclient.h"

/**
 * @class TestWeatherParser
 * @brief Testy jednostkowe dla WeatherApiClient — parsowanie i zapis CSV.
 */
class TestWeatherParser : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Test parsowania poprawnej odpowiedzi geocoding API.
     */
    void testParseGeocodingValid()
    {
        // Przygotuj przykładowy JSON geocoding
        QJsonObject location;
        location["latitude"] = 52.2298;
        location["longitude"] = 21.0118;
        location["name"] = "Warszawa";

        QJsonArray results;
        results.append(location);

        QJsonObject root;
        root["results"] = results;

        QByteArray data = QJsonDocument(root).toJson();

        WeatherApiClient client;
        double lat = 0, lon = 0;
        QString name;

        bool ok = client.parseGeocodingResponse(data, lat, lon, name);

        QVERIFY(ok);
        QCOMPARE(name, QString("Warszawa"));
        QVERIFY(qAbs(lat - 52.2298) < 0.001);
        QVERIFY(qAbs(lon - 21.0118) < 0.001);
    }

    /**
     * @brief Test parsowania pustej odpowiedzi geocoding (brak wyników).
     */
    void testParseGeocodingEmpty()
    {
        QJsonObject root;
        root["results"] = QJsonArray();
        QByteArray data = QJsonDocument(root).toJson();

        WeatherApiClient client;
        double lat = 0, lon = 0;
        QString name;

        bool ok = client.parseGeocodingResponse(data, lat, lon, name);
        QVERIFY(!ok);
    }

    /**
     * @brief Test parsowania nieprawidłowego JSON-a.
     */
    void testParseGeocodingInvalidJson()
    {
        QByteArray data = "this is not json";

        WeatherApiClient client;
        double lat = 0, lon = 0;
        QString name;

        bool ok = client.parseGeocodingResponse(data, lat, lon, name);
        QVERIFY(!ok);
    }

    /**
     * @brief Test parsowania poprawnej odpowiedzi forecast i zapisu do CSV.
     */
    void testParseForecastAndSaveCsv()
    {
        // Przygotuj przykładowy JSON forecast
        QJsonArray times;
        times.append("2025-01-01T00:00");
        times.append("2025-01-01T01:00");
        times.append("2025-01-01T02:00");

        QJsonArray temps;
        temps.append(-2.5);
        temps.append(-3.0);
        temps.append(-2.1);

        QJsonArray precip;
        precip.append(0.0);
        precip.append(1.2);
        precip.append(0.5);

        QJsonArray humidity;
        humidity.append(80);
        humidity.append(82);
        humidity.append(78);

        QJsonArray wind;
        wind.append(5.5);
        wind.append(6.1);
        wind.append(4.9);

        QJsonArray windDir;
        windDir.append(270);
        windDir.append(280);
        windDir.append(260);

        QJsonObject hourly;
        hourly["time"] = times;
        hourly["temperature_2m"] = temps;
        hourly["precipitation"] = precip;
        hourly["relative_humidity_2m"] = humidity;
        hourly["wind_speed_10m"] = wind;
        hourly["wind_direction_10m"] = windDir;

        QJsonObject root;
        root["hourly"] = hourly;

        QByteArray data = QJsonDocument(root).toJson();

        // Zapisz do tymczasowego pliku
        QTemporaryFile tmpFile;
        tmpFile.setAutoRemove(true);
        QVERIFY(tmpFile.open());
        QString tmpPath = tmpFile.fileName();
        tmpFile.close();

        WeatherApiClient client;
        bool ok = client.parseForecastResponse(data, tmpPath);

        QVERIFY(ok);

        // Zweryfikuj zawartość CSV
        QFile csvFile(tmpPath);
        QVERIFY(csvFile.open(QIODevice::ReadOnly | QIODevice::Text));

        QTextStream in(&csvFile);
        QString header = in.readLine();
        QCOMPARE(header, QString("datetime,temperature,precipitation,humidity,wind_speed,wind_dir"));

        QString line1 = in.readLine();
        QStringList cols = line1.split(',');
        QVERIFY(cols.size() == 6);
        QCOMPARE(cols[0], QString("2025-01-01T00:00"));
        QCOMPARE(cols[1].toDouble(), -2.5);
        QCOMPARE(cols[2].toDouble(), 0.0);
        QCOMPARE(cols[3].toDouble(), 80.0);
        QCOMPARE(cols[4].toDouble(), 5.5);
        QCOMPARE(cols[5].toDouble(), 270.0);

        // Powinno być 3 linie danych
        int count = 1;
        while (!in.atEnd()) {
            const QString row = in.readLine();
            if (!row.isEmpty()) {
                count++;
            }
        }
        QCOMPARE(count, 3);
    }

    /**
     * @brief Test parsowania pustego forecast JSON-a.
     */
    void testParseForecastEmpty()
    {
        QJsonObject hourly;
        hourly["time"] = QJsonArray();
        hourly["temperature_2m"] = QJsonArray();

        QJsonObject root;
        root["hourly"] = hourly;

        QByteArray data = QJsonDocument(root).toJson();

        WeatherApiClient client;
        bool ok = client.parseForecastResponse(data, "dummy.csv");
        QVERIFY(!ok);
    }

    /**
     * @brief Test budowania URL-a geocoding.
     */
    void testBuildGeocodingUrl()
    {
        QString url = WeatherApiClient::buildGeocodingUrl("Kraków");
        QVERIFY(url.contains("geocoding-api.open-meteo.com"));
        QVERIFY(url.contains("name=Krak"));
        QVERIFY(url.contains("count=1"));
    }

    /**
     * @brief Test budowania URL-a forecast.
     */
    void testBuildForecastUrl()
    {
        QString url = WeatherApiClient::buildForecastUrl(52.2298, 21.0118, 7);
        QVERIFY(url.contains("api.open-meteo.com"));
        QVERIFY(url.contains("latitude=52.2298"));
        QVERIFY(url.contains("longitude=21.0118"));
        QVERIFY(url.contains("forecast_days=7"));
        QVERIFY(url.contains("hourly=temperature_2m,precipitation,relative_humidity_2m,wind_speed_10m,wind_direction_10m"));
        QVERIFY(url.contains("daily=sunrise,sunset"));
        QVERIFY(url.contains("timezone=auto"));
    }
};

int runTestWeatherParser(int argc, char *argv[])
{
    TestWeatherParser tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_weatherparser.moc"
