#ifndef MAINWINDOW_H
#define MAINWINDOW_H

/**
 * @file mainwindow.h
 * @brief Deklaracja klasy MainWindow odpowiedzialnej za interfejs aplikacji.
 *
 * MainWindow koordynuje przepływ danych między modułami:
 * - WeatherApiClient (pobieranie i parsowanie prognozy),
 * - OllamaClient (generowanie skryptu i porady pogodowe),
 * - ScriptRunner (uruchomienie skryptu i zapis wykresu).
 */

#include <QMainWindow>
#include <QThread>
#include <QPushButton>
#include <QStringList>
#include <QNetworkAccessManager> // potrzebne dla geolokalizacji (locateBtn)
#include <QCompleter>            // autouzupełnianie miast

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class WeatherApiClient;
class OllamaClient;
class ScriptRunner;
class QLabel;
class LoadingSpinner;

/**
 * @class MainWindow
 * @brief Główne okno aplikacji — GUI + orkiestracja modułów.
 *
 * Interfejs zawiera:
 * - pole tekstowe na nazwę miasta z autouzupełnianiem (QCompleter),
 * - przycisk "📍" do automatycznego wykrywania miasta z IP,
 * - spinner na liczbę dni prognozy,
 * - przycisk "Generuj",
 * - animowane kółko ładowania,
 * - obszar wyświetlania wykresu chart.png,
 * - log z komunikatami o statusie,
 * - dynamiczny akcent kolorystyczny zależny od temperatury.
 *
 * Zadania sieciowe (pobieranie pogody, komunikacja z Ollama)
 * wykonywane są w osobnych wątkach QThread.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief Konstruktor głównego okna.
     * @param parent Wskaźnik do widgetu nadrzędnego.
     */
    explicit MainWindow(QWidget *parent = nullptr);

    /**
     * @brief Destruktor — zatrzymuje wątki i sprząta zasoby.
     */
    ~MainWindow() override;

protected:
    /**
     * @brief Reaguje na zmianę rozmiaru okna i przelicza skalowanie wykresu.
     * @param event Zdarzenie resize przekazane przez Qt.
     */
    void resizeEvent(QResizeEvent *event) override;

private slots:
    /** @brief Obsługuje kliknięcie przycisku "Generuj". */
    void onGenerateClicked();

    /** @brief Obsługuje kliknięcie przycisku "Szczegóły dnia". */
    void onGenerateDayClicked();

    /**
     * @brief Obsługuje gotowość danych pogodowych.
     * @param csvPath Ścieżka do pliku CSV z danymi.
     * @param cityName Rozpoznana nazwa miasta.
     */
    void onWeatherReady(const QString &csvPath, const QString &cityName);

    /**
     * @brief Obsługuje wygenerowany skrypt Pythona.
     * @param pythonCode Czysty kod Pythona.
     */
    void onScriptGenerated(const QString &pythonCode);

    /**
     * @brief Obsługuje gotowość wykresu.
     * @param chartPath Ścieżka do pliku chart.png.
     */
    void onChartReady(const QString &chartPath);

    /**
     * @brief Obsługuje błędy z dowolnego modułu.
     * @param message Opis błędu.
     */
    void onError(const QString &message);

    /** @brief Obsługuje kliknięcie przycisku rekomendacji. */
    void onAskClicked();

    /** @brief Zapisuje aktualny wykres do pliku wybranego przez użytkownika. */
    void onSaveChartClicked();

    /** @brief Otwiera okno z kodem Pythona użytym do wygenerowania wykresu. */
    void onShowCodeClicked();

    /** @brief Otwiera okno z historią logów aplikacji. */
    void onShowLogsClicked();

    /**
     * @brief Obsługuje odpowiedź rekomendacji z modelu (koniec streamu lub non-stream).
     * @param text Treść rekomendacji (pusta gdy streaming zakończony).
     */
    void onRecommendationReady(const QString &text);

    /**
     * @brief Obsługuje pojedynczy fragment odpowiedzi streamowanej z Ollama.
     * @param chunk Fragment tekstu ze streamu.
     */
    void onRecommendationChunk(const QString &chunk);

    /**
     * @brief Obsługuje kliknięcie przycisku "Zlokalizuj mnie" (📍).
     *
     * Wysyła GET do ip-api.com, pobiera nazwę miasta z IP
     * i wstawia ją do pola cityInput.
     */
    void onLocateClicked();

    /**
     * @brief Sprawdza dostępność zależności Pythona (pandas, matplotlib).
     *
     * Wywoływana asynchronicznie po starcie okna. Jeśli biblioteki
     * są niedostępne, wyświetla przyjazny QMessageBox z instrukcją.
     */
    void checkPythonDeps();

    /**
     * @brief Obsługuje dane o jakości powietrza (PM2.5) z WeatherApiClient.
     * @param pm25 Średnia wartość PM2.5 w µg/m³ (-1 jeśli niedostępne).
     */
    void onAirQualityReady(double pm25);

private:
    /** @brief Tworzy workery i przenosi je do osobnych wątków. */
    void setupWorkers();

    /** @brief Łączy sygnały i sloty między workerami a GUI. */
    void connectSignals();

    /** @brief Dodaje komunikat do logu statusu. */
    void appendLog(const QString &message);

    /** @brief Ustawia stan interfejsu (aktywny/nieaktywny) podczas generowania. */
    void setGenerating(bool generating);

    /** @brief Skaluje i wyświetla wykres w scrollArea. */
    void updateChartDisplay();

    /** @brief Oblicza i wyświetla statystyki pogodowe dla podanej daty. */
    void calculateAndDisplayStats(const QString &csvPath, const QString &targetDate);

    /**
     * @brief Konfiguruje autouzupełnianie (QCompleter) na polu cityInput.
     *
     * Ładuje wbudowaną listę ~150 miast (stolice świata + polskie miasta).
     */
    void setupCityCompleter();

    /**
     * @brief Stosuje dynamiczny akcent kolorystyczny QGroupBox zależny od temperatury.
     * @param avgTemp Średnia temperatura w °C.
     *
     * > 20°C → pomarańczowe obramowanie; < 0°C → niebieskie; między → domyślne.
     */
    void applyTemperatureTheme(double avgTemp);

    Ui::MainWindow *ui;         ///< Wskaźnik na interfejs użytkownika wygenerowany z pliku .ui.

    // --- Workery i wątki ---
    WeatherApiClient *m_weatherClient;  ///< Klient API pogodowego.
    OllamaClient *m_ollamaClient;       ///< Klient Ollama AI.
    ScriptRunner *m_scriptRunner;       ///< Silnik wykonawczy skryptów.
    QThread *m_weatherThread;           ///< Wątek pobierania pogody.
    QThread *m_ollamaThread;            ///< Wątek komunikacji z Ollama.

    // --- Stan bieżącego zapytania ---
    QString m_currentCsvPath;        ///< Ścieżka do aktualnego pliku CSV.
    QString m_currentCityName;       ///< Nazwa aktualnego miasta.
    QString m_currentTargetDate;     ///< Aktualnie wybrana data szczegółów.
    QPixmap m_currentChartPixmap;    ///< Przechowuje oryginalny wygenerowany wykres.
    QString m_lastGeneratedPythonCode; ///< Ostatni kod Pythona użyty do wygenerowania wykresu.

    // --- Statusbar widgets ---
    QLabel *m_statusLabel;
    QPushButton *m_logsButton;
    LoadingSpinner *m_statusSpinner;
    QStringList m_logHistory;
    QString m_currentStatsSummary;

    // --- Geolokalizacja ---
    QNetworkAccessManager *m_locateManager; ///< Manager do jednorazowego zapytania ip-api.com.

    // --- Streaming Ollama ---
    bool m_streamingFirstChunk; ///< Czy kolejny chunk będzie pierwszym (trzeba dodać nagłówek "AI:").

    // --- Jakość powietrza ---
    double m_currentPm25; ///< Aktualna wartość PM2.5 w µg/m³; -1.0 = nieznana.
};

#endif // MAINWINDOW_H
