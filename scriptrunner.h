#ifndef SCRIPTRUNNER_H
#define SCRIPTRUNNER_H

/**
 * @file scriptrunner.h
 * @brief Moduł wykonawczy — zapis i uruchomienie skryptów Pythona.
 *
 * Klasa ScriptRunner zapisuje kod Pythona do pliku .py,
 * uruchamia go przez QProcess i weryfikuje wynik.
 */

#include <QObject>
#include <QProcess>
#include <QString>

/**
 * @class ScriptRunner
 * @brief Silnik wykonawczy skryptów Python.
 *
 * Odpowiada za:
 * - zapis kodu Pythona do pliku plot_weather.py,
 * - uruchomienie interpretera Python przez QProcess,
 * - weryfikację, czy plik chart.png został wygenerowany.
 */
class ScriptRunner : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Konstruktor silnika wykonawczego.
     * @param parent Wskaźnik do obiektu nadrzędnego Qt.
     */
    explicit ScriptRunner(QObject *parent = nullptr);

    /**
     * @brief Zapisuje kod do pliku i uruchamia go w procesie.
     * @param pythonCode Kod Pythona do wykonania.
     * @param csvPath Ścieżka do pliku CSV (zapisywana na wypadek błędu).
     * @param cityName Nazwa miasta (zapisywana na wypadek błędu).
     * @param targetDate Opcjonalna data filtrowania dla skryptu zapasowego.
     */
    void runScript(const QString &pythonCode, const QString &csvPath = QString(), const QString &cityName = QString(), const QString &targetDate = QString());

    /**
     * @brief Zwraca ścieżkę do wygenerowanego wykresu.
     * @return Absolutna ścieżka do chart.png.
     */
    QString chartPath() const;

    /**
     * @brief Zwraca ścieżkę do pliku skryptu Python.
     * @return Absolutna ścieżka do plot_weather.py.
     */
    QString scriptPath() const;

signals:
    /**
     * @brief Emitowany po pomyślnym wygenerowaniu wykresu.
     * @param chartPath Ścieżka do pliku chart.png.
     */
    void chartReady(const QString &chartPath);

    /**
     * @brief Emitowany w przypadku błędu wykonania skryptu.
     * @param message Opis błędu (w tym stderr z Pythona).
     */
    void errorOccurred(const QString &message);

private slots:
    /**
     * @brief Obsługuje zakończenie procesu Python.
     * @param exitCode Kod zakończenia procesu.
     * @param exitStatus Status zakończenia (normalny/crash).
     */
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /** @brief Generuje awaryjny skrypt Python (hardcoded). */
    QString generateFallbackScript() const;

private:
    QProcess *m_process;    ///< Proces interpretera Python.
    QString m_scriptPath;   ///< Ścieżka do pliku plot_weather.py.
    QString m_chartPath;    ///< Ścieżka do pliku chart.png.
    QString m_workingDir;   ///< Katalog roboczy dla skryptu.
    QString m_csvPath;         ///< Ścieżka do CSV, przydatna dla skryptu zapasowego.
    QString m_cityName;        ///< Nazwa miasta, przydatna dla skryptu zapasowego.
    QString m_targetDate;      ///< Opcjonalna data filtrowania, przydatna dla skryptu zapasowego.
    bool m_usedFallback;       ///< Flaga mówiąca, czy w danym przebiegu użyto już skryptu zapasowego.
};

#endif // SCRIPTRUNNER_H
