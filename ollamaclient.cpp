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
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>

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
 * @brief Wysyła zapytanie do lokalnego modelu Ollama, wzbogacając kontekst o dane z API oraz pliku CSV.
 *
 * Funkcja przygotowuje kompleksowy prompt dla modelu językowego, realizując następujące kroki:
 * 1. Odczytuje i filtruje plik "weather_data.csv" z katalogu aplikacji (pobierając dane co 3 godziny, aby uniknąć przeładowania kontekstu AI).
 * 2. Tłumaczy surowe wartości opadów na logiczne podpowiedzi tekstowe ("TAK/NIE"), co ułatwia mniejszym modelom bezbłędną interpretację.
 * 3. Rozdziela instrukcje na `system` (twarde zasady zachowania, wymuszenie czystego tekstu) oraz główny `prompt` (zawierający dane z API, skrócone dane CSV i właściwe pytanie użytkownika).
 *
 * Zapytanie wysyłane jest z włączonym streamingiem (stream: true). Odpowiedź wraca jako seria linii JSON w formacie:
 * @code
 * {"model":"...","response":"fragment tekstu","done":false}
 * {"model":"...","response":"","done":true}
 * @endcode
 *
 * Zastosowanie sygnału readyRead pozwala przetwarzać fragmenty na bieżąco
 * zamiast czekać na finished, co eliminuje długą pauzę przed wyświetleniem tekstu w interfejsie.
 *
 * @param question       Pytanie zadane przez użytkownika dotyczące prognozy.
 * @param weatherSummary Skrócone, bieżące dane pogodowe pobrane z głównego API, służące jako podstawa kontekstu.
 */

// TAK BYLY Z TYM PROBLEMY

void OllamaClient::askRecommendation(const QString &question, const QString &weatherSummary)
{
    QString csvData = "Szczegółowa prognoza godzinowa:\n";
    QString filePath = QCoreApplication::applicationDirPath() + "/weather_data.csv";
    QFile file(filePath);

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);

        // Pomijamy pierwszą linijkę (nagłówki: datetime,temperature,precipitation...)
        if (!in.atEnd()) {
            in.readLine();
        }

        int lineCounter = 0;
        // Czytamy plik linijka po linijce i formatujemy
        while (!in.atEnd()) {
            QString line = in.readLine();
            QStringList parts = line.split(',');

            // Upewniamy się, że linijka ma wszystkie 6 kolumn
            if (parts.size() >= 6) {
                // Pobieramy tylko co 3-cią linię, żeby odciążyć kontekst modelu
                if (lineCounter % 3 == 0) {
                    QString datetime = parts[0];
                    datetime.replace("T", " o "); // Zmienia "2026-04-23T15:00" na "2026-04-23 o 15:00"

                    QString temp = parts[1];
                    QString opadyRaw = parts[2];
                    QString wiatr = parts[4];
                    // AI JEST TAKIE GŁUPIE
                    QString czyBedziePadac = (opadyRaw.toFloat() > 0.0f) ? QString("TAK (%1 mm)").arg(opadyRaw) : "NIE";

                    csvData += QString("- %1 -> Temp: %2°C, Będzie padać: %3, Wiatr: %4 km/h\n")
                                   .arg(datetime, temp, czyBedziePadac, wiatr);
                }
                lineCounter++;
            }
        }
        file.close();
    } else {
        csvData = "Brak dodatkowych danych godzinowych.";
    }
    // TO WYSYLA DO KONSOLKI INFO O TYM CO OLLAMA DOSTAJE JAK SZUKALEM JAK TO NAPRAWIC BO SIE OLLAMA BUNTOWALA ~ mel
    //qDebug() << "--- WYSYŁANE DO AI ---";
    //qDebug() << "API:" << weatherSummary;
    //qDebug() << "CSV (skrócone):" << csvData;
    //qDebug() << "Pytanie:" << question;
    //qDebug() << "----------------------";

    QJsonObject json;
    json["model"]  = m_modelName;
    json["stream"] = true;

    // 1. SYSTEM PROMPT - Tylko twarde zasady zachowania
    QString systemPrompt = "Jesteś polskim asystentem pogodowym. "
                           "MASZ CAŁKOWITY ZAKAZ używania formatowania Markdown (żadnych gwiazdek, pogrubień, list). "
                           "Odpowiadaj wyłącznie czystym tekstem. "
                           "Nie proś o podanie miasta ani daty. Odpowiadaj krótko i na temat. "
                           "UWAGA: Użytkownik może pytać o datę w formacie DD.MM (np. 23.04), co odpowiada danym w formacie YYYY-MM-DD (np. 2026-04-23).";

    // 2. USER PROMPT - Dostarczenie "teczki z danymi" i właściwego pytania
    QString fullPrompt = QString(
                             "Odpowiedz na pytanie użytkownika na podstawie poniższych danych. "
                             "Jeśli w danych nie ma odpowiedzi, napisz po prostu: 'Brak danych dla tego terminu'.\n\n"
                             "[DANE Z API]:\n%1\n\n"
                             "[DANE Z CSV]:\n%2\n\n"
                             "Pytanie użytkownika: %3"
                             ).arg(weatherSummary).arg(csvData).arg(question);

    json["system"] = systemPrompt;
    json["prompt"] = fullPrompt;

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
            .arg("line"); // chartType.contains("Słupkowy") ? "bar" : "line" TO JEST ARCHIWALNE DLATEGO ZMIANA 
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
            .arg("line"); // chartType.contains("Słupkowy") ? "bar" : "line" SAME THING
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
