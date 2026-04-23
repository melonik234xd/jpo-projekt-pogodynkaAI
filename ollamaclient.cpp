/**
 * @file ollamaclient.cpp
 * @brief Implementacja klienta Ollama REST API.
 *
 * Moduł odpowiada za:
 * - budowanie promptów dla modelu,
 * - wysyłkę zapytań do endpointu /api/generate,
 * - obsługę odpowiedzi zwykłych i strumieniowych.
 */

#include "ollamaclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

/**
 * @brief Tworzy klienta komunikacji z lokalnym serwerem Ollama.
 * @param parent Obiekt nadrzędny Qt.
 */
OllamaClient::OllamaClient(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_ollamaUrl("http://localhost:11434/api/generate")
    , m_modelName("gemma4:e4b")
{
}

/**
 * @brief Wysyła zapytanie o wygenerowanie skryptu Pythona.
 * @param csvPath Ścieżka do danych wejściowych CSV.
 * @param cityName Nazwa miasta używana w tytule wykresu.
 * @param targetDate Opcjonalna data filtrowania.
 * @param chartType Preferowany typ wykresu temperatury.
 */
void OllamaClient::generateScript(const QString &csvPath, const QString &cityName,
                                   const QString &targetDate, const QString &chartType)
{
    QByteArray jsonData = buildRequestJson(csvPath, cityName, targetDate, chartType);

    QUrl ollamaUrl(m_ollamaUrl);
    QNetworkRequest request{ollamaUrl};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(120000);

    QNetworkReply *reply = m_networkManager->post(request, jsonData);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onOllamaReply(reply);
    });
}

/**
 * @brief Wysyła zapytanie do Ollama z włączonym streamingiem.
 *
 * Ollama przy stream:true wysyła odpowiedź jako serię linii JSON, każda w formacie:
 * @code
 * {"model":"...","response":"fragment tekstu","done":false}
 * {"model":"...","response":"","done":true}
 * @endcode
 *
 * Połączenie readyRead pozwala przetwarzać każdy fragment na bieżąco
 * zamiast czekać na finished (co eliminuje długą pauzę przed wyświetleniem).
 *
 * @param question      Pytanie użytkownika.
 * @param weatherSummary Skrócone dane pogodowe dla kontekstu modelu.
 */
void OllamaClient::askRecommendation(const QString &question, const QString &weatherSummary)
{
    QJsonObject json;
    json["model"]  = m_modelName;
    json["stream"] = true;
    json["system"] = QString("Jesteś polskim asystentem pogodowym. Bądź zwięzły i pomocny. "
                             "NIE używaj formatowania Markdown (żadnych gwiazdek, pogrubień ani list). "
                             "Używaj wyłącznie czystego tekstu. Aktualne dane pogodowe to: %1").arg(weatherSummary);
    json["prompt"] = question;

    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    QUrl ollamaUrl(m_ollamaUrl);
    QNetworkRequest request{ollamaUrl};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(120000);

    // Wyczyść bufor przed nowym zapytaniem
    m_streamBuffer.clear();

    QNetworkReply *reply = m_networkManager->post(request, jsonData);

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        m_streamBuffer += reply->readAll();

        while (m_streamBuffer.contains('\n')) {
            int idx = m_streamBuffer.indexOf('\n');
            QByteArray line = m_streamBuffer.left(idx).trimmed();
            m_streamBuffer  = m_streamBuffer.mid(idx + 1);

            if (line.isEmpty()) continue;

            QJsonDocument chunkDoc = QJsonDocument::fromJson(line);
            if (chunkDoc.isNull() || !chunkDoc.isObject()) continue;

            QJsonObject root = chunkDoc.object();
            QString fragment = root.value("response").toString();
            bool    done     = root.value("done").toBool(false);

            if (!fragment.isEmpty()) {
                emit recommendationChunk(fragment);
            }

            if (done) {
                emit recommendationReady(QString());
            }
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() == QNetworkReply::ConnectionRefusedError) {
                emit errorOccurred(
                    "Nie można połączyć się z Ollama. "
                    "Upewnij się, że serwer jest uruchomiony (komenda: ollama serve).");
            } else {
                emit errorOccurred(
                    QString("Błąd pobierania rekomendacji z Ollama: %1")
                        .arg(reply->errorString()));
            }
        }

        m_streamBuffer.clear();
    });
}

/**
 * @brief Buduje payload JSON do generowania kodu wykresu.
 * @param csvPath Ścieżka do pliku CSV.
 * @param cityName Nazwa miasta do tytułu wykresu.
 * @param targetDate Opcjonalna data filtrowania danych.
 * @param chartType Preferowany typ wykresu temperatury.
 * @return JSON gotowy do wysyłki metodą POST.
 */

QByteArray OllamaClient::buildRequestJson(const QString &csvPath, const QString &cityName,
                                           const QString &targetDate, const QString &chartType) const
{
    QJsonObject json;
    json["model"]  = m_modelName;
    json["stream"] = false;

    json["system"] = QString(
        "You are a Python code generator. Output ONLY valid Python code. "
        "No markdown, no backticks, no explanations. "
        "Do NOT use triple-quoted strings or docstrings. "
        "Do NOT use any comments. Just executable Python code.");

    QString prompt;
    if (targetDate.isEmpty()) {
        prompt = QString(
            "Write a short Python script. It must:\n"
            "- import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt; import pandas as pd; import matplotlib.dates as mdates\n"
            "- Read CSV file '%1' with columns datetime,temperature,precipitation,humidity,wind_speed using pd.read_csv()\n"
            "- Convert datetime column with pd.to_datetime()\n"
            "- Use plt.style.use('ggplot')\n"
            "- Create a figure with 2 subplots stacked vertically: fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 10))\n"
            "- ax1: plot temperature vs datetime as a %3 chart (color '#e74c3c'), set title 'Temperatura', ylabel 'C'\n"
            "- ax2: plot precipitation vs datetime as a bar chart (color '#3498db', width 0.04), set title 'Opady', ylabel 'mm'\n"
            "- Format x-axis for BOTH axes: ax1.xaxis.set_major_formatter(mdates.DateFormatter('%d.%m %H:00')) and same for ax2\n"
            "- Main title: 'Prognoza pogody - %2' using fig.suptitle()\n"
            "- Add grid to both, use ax1.tick_params(axis='x', rotation=45) and ax2.tick_params(axis='x', rotation=45)\n"
            "- plt.tight_layout(); plt.subplots_adjust(top=0.9, hspace=0.4)\n"
            "- plt.savefig('chart.png', dpi=100)\n"
            "- Do NOT call plt.show()\n"
            "Output ONLY the Python code, nothing else.")
            .arg(csvPath)
            .arg(cityName)
            .arg(chartType.contains("Słupkowy") ? "bar" : "line");
    } else {
        prompt = QString(
            "Write a short Python script. It must:\n"
            "- import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt; import pandas as pd; import matplotlib.dates as mdates\n"
            "- Read CSV file '%1' with columns datetime,temperature,precipitation,humidity,wind_speed using pd.read_csv()\n"
            "- Convert datetime column with pd.to_datetime()\n"
            "- Filter df to include data from exactly 12 hours BEFORE %2 00:00 to 36 hours AFTER %2 00:00.\n"
            "- Use plt.style.use('ggplot')\n"
            "- Create a figure with 2 subplots stacked vertically: fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 10))\n"
            "- ax1: plot temperature vs datetime as a %4 chart (color '#e74c3c'), set title 'Temperatura', ylabel 'C'\n"
            "- ax2: plot precipitation vs datetime as a bar chart (color '#3498db', width 0.04), set title 'Opady', ylabel 'mm'\n"
            "- Format x-axis for BOTH axes: ax1.xaxis.set_major_formatter(mdates.DateFormatter('%d.%m %H:00')) and same for ax2\n"
            "- Main title: 'Szczegóły pogody dla %2 - %3' using fig.suptitle()\n"
            "- Add grid to both, use ax1.tick_params(axis='x', rotation=45) and ax2.tick_params(axis='x', rotation=45)\n"
            "- plt.tight_layout(); plt.subplots_adjust(top=0.9, hspace=0.4)\n"
            "- plt.savefig('chart.png', dpi=100)\n"
            "- Do NOT call plt.show()\n"
            "Output ONLY the Python code, nothing else.")
            .arg(csvPath)
            .arg(targetDate)
            .arg(cityName)
            .arg(chartType.contains("Słupkowy") ? "bar" : "line");
    }

    json["prompt"] = prompt;
    QJsonDocument docOut(json);
    return docOut.toJson(QJsonDocument::Compact);
}

/**
 * @brief Usuwa otoczkę markdown i zwraca sam kod Pythona.
 * @param rawResponse Surowa odpowiedź tekstowa modelu.
 * @return Oczyszczony kod gotowy do uruchomienia.
 */
QString OllamaClient::extractPythonCode(const QString &rawResponse)
{
    QString code = rawResponse.trimmed();

    QRegularExpression codeBlockRegex(
        R"(```(?:python)?\s*\n([\s\S]*?)```)",
        QRegularExpression::MultilineOption);
    QRegularExpressionMatch match = codeBlockRegex.match(code);
    if (match.hasMatch()) {
        code = match.captured(1).trimmed();
    }

    QStringList lines = code.split('\n');
    int startIdx = 0;
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        if (line.startsWith("import ") || line.startsWith("from ") ||
            line.startsWith("#") || line.startsWith("\"\"\"") ||
            line.startsWith("import") || line.isEmpty()) {
            startIdx = i;
            break;
        }
    }
    if (startIdx > 0) {
        lines = lines.mid(startIdx);
        code = lines.join('\n');
    }
    return code.trimmed();
}

/**
 * @brief Obsługuje odpowiedź non-streaming dla generateScript().
 * @param reply Odpowiedź HTTP z endpointu Ollama.
 */
void OllamaClient::onOllamaReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg;
        if (reply->error() == QNetworkReply::ConnectionRefusedError) {
            errorMsg = "Nie można połączyć się z Ollama. "
                       "Upewnij się, że serwer Ollama jest uruchomiony "
                       "(komenda: ollama serve).";
        } else {
            errorMsg = QString("Błąd komunikacji z Ollama: %1").arg(reply->errorString());
        }
        emit errorOccurred(errorMsg);
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (doc.isNull() || !doc.isObject()) {
        emit errorOccurred("Nieprawidłowa odpowiedź z Ollama (nie jest JSON).");
        return;
    }

    QJsonObject root = doc.object();
    if (root.contains("error")) {
        emit errorOccurred(
            QString("Ollama zwróciła błąd: %1").arg(root.value("error").toString()));
        return;
    }

    QString rawResponse = root.value("response").toString();
    if (rawResponse.trimmed().isEmpty()) {
        emit errorOccurred("Ollama zwróciła pustą odpowiedź.");
        return;
    }

    QString pythonCode = extractPythonCode(rawResponse);
    if (pythonCode.isEmpty()) {
        emit errorOccurred("Nie udało się wyekstrahować kodu Pythona z odpowiedzi modelu.");
        return;
    }

    emit scriptGenerated(pythonCode);
}
