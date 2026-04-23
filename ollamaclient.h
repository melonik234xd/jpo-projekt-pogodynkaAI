#ifndef OLLAMACLIENT_H
#define OLLAMACLIENT_H

/**
 * @file ollamaclient.h
 * @brief Moduł komunikacji z lokalnym modelem AI (Ollama).
 *
 * Klasa OllamaClient buduje prompt i wysyła zapytanie do
 * endpointu /api/generate serwera Ollama, aby wygenerować
 * skrypt Pythona rysujący wykres pogodowy lub udzielić
 * porad pogodowych w trybie streamingu.
 *
 * @note askRecommendation() używa stream:true.
 *       Zamiast jednego sygnału recommendationReady emituje
 *       kolejno recommendationChunk() dla każdego fragmentu
 *       i recommendationReady("") po zakończeniu.
 */

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QByteArray>

/**
 * @class OllamaClient
 * @brief Klient REST API Ollama do generowania kodu Pythona i porad.
 *
 * Wysyła zapytanie POST do http://localhost:11434/api/generate
 * z promptem systemowym (generator kodu) i promptem użytkownika.
 *
 * Tryby:
 * - generateScript(): stream:false, emituje scriptGenerated().
 * - askRecommendation(): stream:true, emituje ciąg recommendationChunk()
 *   a na końcu recommendationReady("").
 */
class OllamaClient : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Konstruktor klienta Ollama.
     * @param parent Wskaźnik do obiektu nadrzędnego Qt.
     */
    explicit OllamaClient(QObject *parent = nullptr);

public slots:
    /**
     * @brief Zleca asynchroniczne wygenerowanie skryptu przez lokalny model Ollama.
     * @param csvPath Ścieżka do pliku CSV.
     * @param cityName Nazwa miasta.
     * @param targetDate Opcjonalna data (np. "2024-04-18").
     * @param chartType Typ wykresu (np. "Liniowy" lub "Słupkowy").
     */
    void generateScript(const QString &csvPath, const QString &cityName,
                        const QString &targetDate = QString(),
                        const QString &chartType  = "Liniowy");

    /**
     * @brief Pyta model o rekomendacje pogodowe — używa trybu streamingu.
     *
     * Wyniki przychodzą jako seria sygnałów recommendationChunk(),
     * a po zakończeniu emitowany jest recommendationReady("").
     *
     * @param question Pytanie użytkownika.
     * @param weatherSummary Krótkie podsumowanie danych pogodowych.
     */
    void askRecommendation(const QString &question, const QString &weatherSummary);

public:
    /**
     * @brief Buduje JSON z promptem dla modelu (generowanie wykresu).
     * @param csvPath Ścieżka do pliku CSV.
     * @param cityName Nazwa miasta.
     * @param targetDate Wybrana data.
     * @param chartType Typ wykresu.
     * @return Bajty JSON gotowe do wysłania.
     */
    QByteArray buildRequestJson(const QString &csvPath, const QString &cityName,
                                const QString &targetDate = QString(),
                                const QString &chartType  = "Liniowy") const;

    /**
     * @brief Wyciąga czysty kod Pythona z odpowiedzi modelu.
     * @param rawResponse Surowa odpowiedź tekstowa z modelu.
     * @return Czysty kod Pythona (bez bloków markdown, komentarzy wstępnych).
     */
    static QString extractPythonCode(const QString &rawResponse);

signals:
    /**
     * @brief Emitowany po pomyślnym wygenerowaniu kodu Pythona.
     * @param pythonCode Wyekstrahowany czysty kod Pythona.
     */
    void scriptGenerated(const QString &pythonCode);

    /**
     * @brief Emitowany dla każdego fragmentu ze streamu odpowiedzi.
     *
     * Wywoływany wielokrotnie podczas generowania odpowiedzi przez model.
     * Ostatni fragment poprzedza sygnał recommendationReady("").
     *
     * @param text Fragment tekstu odpowiedzi.
     */
    void recommendationChunk(const QString &text);

    /**
     * @brief Emitowany po zakończeniu odpowiedzi modelu.
     *
     * W trybie streamingu: text jest pustym stringiem — faktyczne dane
     * przyszły przez recommendationChunk().
     * W trybie non-streaming (fallback): text zawiera pełną odpowiedź.
     *
     * @param text Pełny tekst (non-streaming) lub "" (streaming).
     */
    void recommendationReady(const QString &text);

    /**
     * @brief Emitowany w przypadku błędu komunikacji z Ollama.
     * @param message Opis błędu.
     */
    void errorOccurred(const QString &message);

private slots:
    /** @brief Obsługuje odpowiedź z Ollama API (generateScript). */
    void onOllamaReply(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_networkManager; ///< Menedżer połączeń sieciowych.
    QString m_ollamaUrl;   ///< URL endpointu Ollama.
    QString m_modelName;   ///< Nazwa modelu (np. "gemma4:e4b").

    /**
     * @brief Bufor akumulujący dane podczas streamingu.
     *
     * Ollama wysyła dane strumieniowo jako linie JSON oddzielone \\n.
     * Bufor zbiera bajty z kolejnych sygnałów readyRead i przetwarza
     * kompletne linie, pozostawiając niekompletne do następnego wywołania.
     */
    QByteArray m_streamBuffer;
};

#endif // OLLAMACLIENT_H
