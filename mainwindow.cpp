/**
 * @file mainwindow.cpp
 * @brief Implementacja klasy MainWindow odpowiedzialnej za logikę GUI.
 *
 * Plik zawiera obsługę:
 * - inicjalizacji interfejsu i workerów,
 * - połączeń sygnałów/slotów między warstwą GUI i usługami,
 * - cyklu pobrania danych pogodowych i generowania wykresu,
 * - streamingu odpowiedzi AI oraz prezentacji statystyk pogodowych.
 */

#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "weatherapiclient.h"
#include "ollamaclient.h"
#include "scriptrunner.h"

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFontDatabase>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QTextStream>
#include <QVBoxLayout>
#include <cmath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QSettings>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QProcess>
#include <QNetworkReply>

/**
 * @class LoadingSpinner
 * @brief Prosty wskaźnik aktywności renderowany jako obracający się łuk.
 *
 * Klasa jest używana wyłącznie w MainWindow do sygnalizacji trwającej
 * operacji asynchronicznej (sieć, generowanie kodu, uruchomienie skryptu).
 */

class LoadingSpinner : public QWidget
{
public:
    /**
     * @brief Tworzy spinner i uruchamia timer animacji.
     * @param parent Widget nadrzędny.
     */
    explicit LoadingSpinner(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_angle(0)
        , m_timer(new QTimer(this))
    {
        setFixedSize(18, 18);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_timer->setInterval(80);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            m_angle = (m_angle + 30) % 360;
            update();
        });
        hide();
    }

    /** @brief Pokazuje spinner i uruchamia animację. */
    void startSpinning()
    {
        if (!m_timer->isActive()) m_timer->start();
        show();
    }

    /** @brief Zatrzymuje animację i ukrywa spinner. */
    void stopSpinning()
    {
        m_timer->stop();
        hide();
    }

protected:
    /**
     * @brief Rysuje bieżącą klatkę spinnera.
     * @param event Zdarzenie malowania.
     */
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const int side = (width() < height()) ? width() : height();
        const qreal margin = 2.0;
        const QRectF arcRect(
            (width() - side) / 2.0 + margin,
            (height() - side) / 2.0 + margin,
            side - (margin * 2.0),
            side - (margin * 2.0)
        );

        QPen trackPen(QColor(180, 180, 180, 90), 2.0);
        trackPen.setCapStyle(Qt::RoundCap);
        painter.setPen(trackPen);
        painter.drawArc(arcRect, 0, 360 * 16);

        QPen activePen(QColor(255, 90, 90), 2.2);
        activePen.setCapStyle(Qt::RoundCap);
        painter.setPen(activePen);
        painter.drawArc(arcRect, (90 - m_angle) * 16, -110 * 16);
    }

private:
    int m_angle;
    QTimer *m_timer;
};

/**
 * @brief Inicjalizuje główne okno, statusbar i konfigurację startową.
 * @param parent Widget nadrzędny.
 */

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_weatherClient(nullptr)
    , m_ollamaClient(nullptr)
    , m_scriptRunner(nullptr)
    , m_weatherThread(nullptr)
    , m_ollamaThread(nullptr)
    , m_statusLabel(nullptr)
    , m_logsButton(nullptr)
    , m_statusSpinner(nullptr)
    , m_locateManager(new QNetworkAccessManager(this))
    , m_streamingFirstChunk(true)
    , m_currentPm25(-1.0)
{
    ui->setupUi(this);

    ui->chatGroup->setEnabled(false);
    ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Dolny pasek statusu
    m_logsButton = new QPushButton("Logi", this);
    m_logsButton->setToolTip("Pokaż historię logów aplikacji");
    m_logsButton->setFocusPolicy(Qt::NoFocus);
    m_logsButton->setFixedHeight(22);
    m_statusLabel = new QLabel("[--:--:--] Gotowe", this);
    m_statusSpinner = new LoadingSpinner(this);
    statusBar()->addWidget(m_logsButton, 0);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_statusSpinner);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setStyleSheet("QStatusBar::item { border: none; }");

    ui->statsLabel->setWordWrap(true);

    setupWorkers();
    connectSignals();

    setupCityCompleter();

    // Wczytywanie ustawień
    QSettings settings("PogodynkaAI", "Settings");
    ui->cityInput->setText(settings.value("city", "Warszawa").toString());
    ui->daysInput->setValue(settings.value("days", 7).toInt());

    QTimer::singleShot(500, this, &MainWindow::checkPythonDeps);
}

/** @brief Zamyka wątki workerów i zwalnia zasoby okna. */
MainWindow::~MainWindow()
{
    if (m_weatherThread) { m_weatherThread->quit(); m_weatherThread->wait(); }
    if (m_ollamaThread)  { m_ollamaThread->quit();  m_ollamaThread->wait();  }
    delete ui;
}

/**
 * @brief Tworzy instancje workerów i uruchamia dedykowane wątki robocze.
 */

void MainWindow::setupWorkers()
{
    m_weatherClient = new WeatherApiClient();
    m_ollamaClient  = new OllamaClient();
    m_scriptRunner  = new ScriptRunner();

    m_weatherThread = new QThread(this);
    m_ollamaThread  = new QThread(this);

    m_weatherClient->moveToThread(m_weatherThread);
    m_ollamaClient->moveToThread(m_ollamaThread);
    // ScriptRunner zostaje w głównym wątku — QProcess działa asynchronicznie

    m_weatherThread->start();
    m_ollamaThread->start();
}

/**
 * @brief Łączy sygnały i sloty pomiędzy kontrolkami GUI oraz workerami.
 */
void MainWindow::connectSignals()
{
    connect(m_logsButton, &QPushButton::clicked, this, &MainWindow::onShowLogsClicked);

    connect(ui->generateBtn,  &QPushButton::clicked,     this, &MainWindow::onGenerateClicked);
    connect(ui->cityInput,    &QLineEdit::returnPressed,  this, &MainWindow::onGenerateClicked);
    connect(ui->generateDayBtn, &QPushButton::clicked,   this, &MainWindow::onGenerateDayClicked);
    connect(ui->saveChartBtn, &QPushButton::clicked,     this, &MainWindow::onSaveChartClicked);
    connect(ui->showCodeBtn,  &QPushButton::clicked,     this, &MainWindow::onShowCodeClicked);

    connect(ui->locateBtn, &QPushButton::clicked, this, &MainWindow::onLocateClicked);

    // Pipeline: Weather → Ollama → ScriptRunner → GUI
    connect(m_weatherClient, &WeatherApiClient::dataReady,
            this, &MainWindow::onWeatherReady);
    connect(m_weatherClient, &WeatherApiClient::errorOccurred,
            this, &MainWindow::onError);

    connect(m_weatherClient, &WeatherApiClient::airQualityReady,
            this, &MainWindow::onAirQualityReady);

    connect(m_ollamaClient, &OllamaClient::scriptGenerated,
            this, &MainWindow::onScriptGenerated);
    connect(m_ollamaClient, &OllamaClient::recommendationReady,
            this, &MainWindow::onRecommendationReady);
    connect(m_ollamaClient, &OllamaClient::recommendationChunk,
            this, &MainWindow::onRecommendationChunk);
    connect(m_ollamaClient, &OllamaClient::errorOccurred,
            this, &MainWindow::onError);

    connect(ui->chatSendBtn, &QPushButton::clicked,     this, &MainWindow::onAskClicked);
    connect(ui->chatInput,   &QLineEdit::returnPressed, this, &MainWindow::onAskClicked);

    connect(m_scriptRunner, &ScriptRunner::chartReady,      this, &MainWindow::onChartReady);
    connect(m_scriptRunner, &ScriptRunner::errorOccurred,   this, &MainWindow::onError);

    connect(m_weatherThread, &QThread::finished, m_weatherClient, &QObject::deleteLater);
    connect(m_ollamaThread,  &QThread::finished, m_ollamaClient,  &QObject::deleteLater);
}

/**
 * @brief Asynchronicznie sprawdza, czy pandas i matplotlib są dostępne.
 *
 * Uruchamia `python -c "import pandas; import matplotlib"` przez QProcess.
 * Jeśli brakuje bibliotek, wyświetla QMessageBox z instrukcją pip install.
 * Wywoływana raz po starcie aplikacji przez QTimer::singleShot.
 */
void MainWindow::checkPythonDeps()
{
    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus status) {
                proc->deleteLater();
                if (exitCode != 0 || status != QProcess::NormalExit) {
                    QString errorOutput = proc->readAllStandardError();
                    QMessageBox::warning(
                        this,
                        "Brakujące zależności Pythona",
                        "Nie znaleziono wymaganych bibliotek Pythona.\n\n"
                        "Zainstaluj je, uruchamiając w terminalu:\n\n"
                        "    pip install pandas matplotlib\n\n"
                        "Następnie uruchom aplikację ponownie.\n\n"
                        "Szczegóły błędu:\n" + errorOutput.left(300)
                        );
                } else {
                    appendLog("OK: Zależności Pythona (pandas, matplotlib) dostępne.");
                }
            });
    proc->start("python", {"-c", "import pandas; import matplotlib"});
    if (!proc->waitForStarted(3000)) {
        proc->deleteLater();
        QMessageBox::warning(
            this,
            "Brak Pythona",
            "Nie znaleziono interpretera Python w PATH.\n\n"
            "Upewnij się, że Python 3.x jest zainstalowany i dostępny w PATH.\n"
            "Pobierz go ze strony: https://www.python.org/downloads/"
        );
    }
}

/**
 * @brief Pobiera miasto użytkownika z adresu IP przez ip-api.com.
 *
 * Wysyła GET http://ip-api.com/json/?fields=status,city i wstawia
 * wynik do ui->cityInput. Wyświetla komunikat w przypadku błędu.
 */
void MainWindow::onLocateClicked()
{
    ui->locateBtn->setEnabled(false);
    appendLog("Wykrywanie lokalizacji z adresu IP...");

    QNetworkReply *reply = m_locateManager->get(
        QNetworkRequest(QUrl("http://ip-api.com/json/?fields=status,city"))
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        ui->locateBtn->setEnabled(true);
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "Błąd geolokalizacji",
                QString("Nie udało się pobrać lokalizacji:\n%1").arg(reply->errorString()));
            appendLog("BŁĄD: Geolokalizacja nieudana.");
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) {
            appendLog("BŁĄD: Nieprawidłowa odpowiedź z ip-api.com.");
            return;
        }

        QJsonObject root = doc.object();
        if (root.value("status").toString() != "success") {
            appendLog("BŁĄD: ip-api.com zwróciło status niepowodzenia.");
            QMessageBox::warning(this, "Geolokalizacja",
                "Nie udało się ustalić lokalizacji. Spróbuj wpisać miasto ręcznie.");
            return;
        }

        QString city = root.value("city").toString();
        if (city.isEmpty()) {
            appendLog("BŁĄD: ip-api.com zwróciło puste pole 'city'.");
            return;
        }

        ui->cityInput->setText(city);
        appendLog(QString("OK: Wykryto miasto: %1").arg(city));
    });
}

/**
 * @brief Konfiguruje QCompleter z listą stolic świata i miast Polski.
 *
 * Lista zawiera ~100 stolic w polskiej pisowni oraz ~50 miast polskich.
 * Dopasowanie działa na zasadzie MatchContains (wpisanie fragmentu
 * wystarczy, np. "kra" → "Kraków").
 */
void MainWindow::setupCityCompleter()
{
    // Stolice świata (polska pisownia)
    QStringList cities = {
        // Europa
        "Warszawa", "Londyn", "Paryż", "Berlin", "Rzym", "Madryt", "Lizbona",
        "Amsterdam", "Bruksela", "Wiedeń", "Praga", "Budapeszt", "Bratysława",
        "Bukareszt", "Sofia", "Ateny", "Helsinki", "Sztokholm", "Oslo",
        "Kopenhaga", "Dublin", "Reykjavik", "Berno", "Luksemburg", "Valletta",
        "Nikozja", "Ryga", "Wilno", "Tallinn", "Mińsk", "Kijów", "Kiszyniów",
        "Sarajewo", "Belgrad", "Zagrzeb", "Lublana", "Podgorica", "Skopje",
        "Tirana", "Ankara", "Moskwa", "Tbilisi", "Erywań", "Baku",
        "Monako", "Vaduz", "San Marino", "Andora", "Nikosia",
        // Azja
        "Astana", "Taszkent", "Biszkek", "Duszanbe", "Aszchabad",
        "Kabul", "Islamabad", "Nowe Delhi", "Kolombo", "Dhaka", "Katmandu",
        "Pekin", "Tokio", "Seul", "Singapur", "Dżakarta", "Manila",
        "Kuala Lumpur", "Bangkok", "Hanoi", "Phnom Penh", "Wientian",
        "Bagdad", "Teheran", "Rijad", "Amman", "Bejrut", "Damaszek",
        "Jerozolima", "Abu Zabi", "Doha", "Kuwait",
        // Afryka
        "Kair", "Chartum", "Addis Abeba", "Nairobi", "Kampala",
        "Dar es Salaam", "Harare", "Lusaka", "Pretoria", "Kapsztad",
        "Johannesburg", "Lagos", "Akra", "Dakar", "Abidżan",
        "Bamako", "Niamej", "Mogadiszu", "Rabat", "Algier", "Tunis", "Trypolis",
        // Ameryki
        "Waszyngton", "Ottawa", "Meksyk", "Bogota", "Caracas", "Lima",
        "La Paz", "Santiago", "Buenos Aires", "Montevideo", "Brasilia",
        "Quito", "Asuncion", "Georgetown", "Paramaribo",
        // Oceania
        "Canberra", "Wellington",

        // Popularne niekapitalne metropolie
        "Nowy Jork", "Los Angeles", "Chicago", "Toronto", "Vancouver",
        "São Paulo", "Rio de Janeiro", "Barcelona", "Mediolan", "Neapol",
        "Monachium", "Hamburg", "Frankfurt", "Dortmund", "Kolonia",
        "Marsylia", "Lyon", "Porto", "Sewilla", "Walencja",
        "Edynburg", "Manchester", "Birmingham", "Liverpool",
        "Rotterdam", "Antwerpia", "Zurych", "Genewa", "Bazylea",
        "Dubaj", "Stambuł", "Ankara", "Szanghaj", "Guangzhou", "Shenzhen",
        "Osaka", "Kioto", "Kobe", "Hongkong", "Tajpej",
        "Sydney", "Melbourne", "Brisbane", "Auckland",

        // Polskie miasta (≥50 największych/znanych/nasze xD)
        "Kraków", "Łódź", "Wrocław", "Poznań", "Gdańsk", "Szczecin",
        "Bydgoszcz", "Lublin", "Katowice", "Białystok", "Gdynia",
        "Częstochowa", "Radom", "Sosnowiec", "Toruń", "Kielce",
        "Gliwice", "Zabrze", "Bytom", "Olsztyn", "Bielsko-Biała",
        "Rzeszów", "Ruda Śląska", "Rybnik", "Tychy", "Dąbrowa Górnicza",
        "Opole", "Elbląg", "Płock", "Wałbrzych", "Włocławek",
        "Tarnów", "Chorzów", "Koszalin", "Kalisz", "Legnica",
        "Grudziądz", "Jaworzno", "Słupsk", "Jastrzębie-Zdrój",
        "Nowy Sącz", "Jelenia Góra", "Siedlce", "Mysłowice",
        "Piotrków Trybunalski", "Lubin", "Inowrocław",
        "Zielona Góra", "Zakopane", "Sopot", "Kołobrzeg", "Suwałki",
        "Zamość", "Sanok", "Przemyśl", "Nowy Targ", "Tarnowskie Góry", 
        "Skórzewo", "Gniezno", "Konin" // musiałem dodać xD sorry Kubuś ~mel 
    };

    cities.removeDuplicates();
    cities.sort(Qt::CaseInsensitive);

    auto *completer = new QCompleter(cities, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);   // dopasowanie fragmentu
    completer->setCompletionMode(QCompleter::PopupCompletion);
    ui->cityInput->setCompleter(completer);
}

/**
 * @brief Ustawia styl obramowania QGroupBox zależnie od średniej temperatury.
 *
 * @param avgTemp Średnia temperatura w °C.
 *  - > 20°C → pomarańczowe obramowanie (#FF8C42)
 *  - <  0°C → jasnoniebieski (#64B5F6)
 *  - 0–20°C → domyślny styl (reset)
 */
void MainWindow::applyTemperatureTheme(double avgTemp)
{
    QString accentColor;
    if (avgTemp > 20.0) {
        accentColor = "#FF8C42"; // ciepło — pomarańcz
    } else if (avgTemp < 0.0) {
        accentColor = "#64B5F6"; // mróz — jasny niebieski
    }

    QString groupBoxStyle;
    if (!accentColor.isEmpty()) {
        groupBoxStyle = QString(
            "QGroupBox {"
            "  border: 2px solid %1;"
            "  border-radius: 6px;"
            "  margin-top: 8px;"
            "}"
            "QGroupBox::title {"
            "  subcontrol-origin: margin;"
            "  left: 10px;"
            "  padding: 0 4px;"
            "  color: %1;"
            "}"
        ).arg(accentColor);
    }

    ui->controlGroup->setStyleSheet(groupBoxStyle);
    ui->statsGroup->setStyleSheet(groupBoxStyle);
    ui->chartGroup->setStyleSheet(groupBoxStyle);
    ui->chatGroup->setStyleSheet(groupBoxStyle);
}

/**
 * @brief Odbiera dane PM2.5 i aktualizuje statsLabel.
 *
 * Wywoływana przez sygnał WeatherApiClient::airQualityReady.
 * Przechowuje pm25 i odbudowuje cały blok statystyk.
 *
 * @param pm25 Średnia PM2.5 w µg/m³; -1.0 jeśli dane niedostępne.
 */
void MainWindow::onAirQualityReady(double pm25)
{
    m_currentPm25 = pm25;
    appendLog(pm25 >= 0
        ? QString("OK: Jakość powietrza PM2.5 = %1 µg/m³").arg(pm25, 0, 'f', 1)
        : "INFO: Dane PM2.5 niedostępne.");

    // Odbuduj statystyki z nową linią AQI (tylko gdy mamy CSV)
    if (!m_currentCsvPath.isEmpty()) {
        calculateAndDisplayStats(m_currentCsvPath, m_currentTargetDate);
    }
}

/**
 * @brief Rozpoczyna pełny pipeline generowania prognozy dla wpisanego miasta.
 *
 * Funkcja waliduje dane wejściowe, zapisuje ustawienia użytkownika,
 * resetuje bieżący stan UI i zleca pobranie danych pogodowych.
 */

void MainWindow::onGenerateClicked()
{
    QString city = ui->cityInput->text().trimmed();
    if (city.isEmpty()) {
        QMessageBox::warning(this, "Uwaga", "Wpisz nazwę miasta!");
        return;
    }

    int days = ui->daysInput->value();
    QSettings settings("PogodynkaAI", "Settings");
    settings.setValue("city", city);
    settings.setValue("days", days);

    m_currentTargetDate.clear();
    m_lastGeneratedPythonCode.clear();
    m_currentPm25 = -1.0; // reset AQI przy każdym nowym zapytaniu
    ui->showCodeBtn->setEnabled(false);

    // Wyczyść stary wykres i pokaż komunikat ładowania
    m_currentChartPixmap = QPixmap();
    ui->chartLabel->setPixmap(QPixmap());
    ui->chartLabel->setText(
        QString("Trwa generowanie wykresu temperatury i opadów dla: %1").arg(city));
    ui->chartLabel->setAlignment(Qt::AlignCenter);

    setGenerating(true);
    appendLog(QString("Start: pobieranie pogody dla \"%1\" (%2 dni)...").arg(city).arg(days));

    QMetaObject::invokeMethod(m_weatherClient, "fetchWeather",
                              Qt::QueuedConnection,
                              Q_ARG(QString, city),
                              Q_ARG(int, days));
}

/**
 * @brief Obsługuje odebranie gotowych danych pogodowych.
 * @param csvPath Ścieżka do pliku CSV z danymi godzinowymi.
 * @param cityName Nazwa miasta rozpoznana przez geocoding.
 *
 * Funkcja odświeża listę dostępnych dni i uruchamia generowanie
 * skryptu Pythona przez moduł AI.
 */
void MainWindow::onWeatherReady(const QString &csvPath, const QString &cityName)
{
    m_currentCsvPath = csvPath;
    m_currentCityName = cityName;

    ui->daySelector->clear();
    QFile file(csvPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        in.readLine();
        QStringList dates;
        while (!in.atEnd()) {
            QString line = in.readLine();
            QStringList parts = line.split(',');
            if (!parts.isEmpty()) {
                QString datePart = parts[0].split('T').first();
                if (!dates.contains(datePart)) dates.append(datePart);
            }
        }
        ui->daySelector->addItems(dates);
        file.close();
    }

    appendLog(QString("OK: Dane pogodowe zapisane: %1").arg(csvPath));

    // Natychmiastowe wyświetlenie statystyk — bez czekania na AI i Python
    calculateAndDisplayStats(csvPath, "");

    appendLog("Wysyłanie zapytania do Ollama (generowanie skryptu Python)...");

    QMetaObject::invokeMethod(m_ollamaClient, "generateScript",
                              Qt::QueuedConnection,
                              Q_ARG(QString, csvPath),
                              Q_ARG(QString, cityName),
                              Q_ARG(QString, m_currentTargetDate),
                              Q_ARG(QString, QStringLiteral("Temperatura - Liniowy")));
}

/**
 * @brief Generuje wykres dla konkretnego dnia wybranego z listy.
 *
 * Metoda zachowuje aktualnie pobrany CSV i zleca modelowi AI wygenerowanie
 * skryptu z filtrem czasu wokół wybranej daty.
 */
void MainWindow::onGenerateDayClicked()
{
    QString selectedDate = ui->daySelector->currentText();
    if (selectedDate.isEmpty()) {
        QMessageBox::warning(this, "Uwaga", "Brak wybranej daty!");
        return;
    }

    m_currentTargetDate = selectedDate;
    m_lastGeneratedPythonCode.clear();
    ui->showCodeBtn->setEnabled(false);

    setGenerating(true);
    appendLog(QString("Generowanie szczegółów dla dnia: %1").arg(selectedDate));

    // Natychmiastowe wyświetlenie statystyk dla wybranego dnia
    calculateAndDisplayStats(m_currentCsvPath, selectedDate);

    // Wyczyść stary wykres i pokaż komunikat ładowania
    m_currentChartPixmap = QPixmap();
    ui->chartLabel->setPixmap(QPixmap());
    ui->chartLabel->setText(
        QString("Trwa generowanie wykresu temperatury i opadów dla: %1").arg(m_currentCityName));
    ui->chartLabel->setAlignment(Qt::AlignCenter);

    QMetaObject::invokeMethod(m_ollamaClient, "generateScript",
                              Qt::QueuedConnection,
                              Q_ARG(QString, m_currentCsvPath),
                              Q_ARG(QString, m_currentCityName),
                              Q_ARG(QString, m_currentTargetDate),
                              Q_ARG(QString, QStringLiteral("Temperatura - Liniowy")));
}

/**
 * @brief Odbiera kod Pythona wygenerowany przez model i uruchamia wykonanie.
 * @param pythonCode Kod źródłowy skryptu Pythona.
 */
void MainWindow::onScriptGenerated(const QString &pythonCode)
{
    m_lastGeneratedPythonCode = pythonCode;
    appendLog("OK: Kod Pythona wygenerowany przez model AI.");
    appendLog("Wykonywanie skryptu Python (generowanie wykresu)...");
    m_scriptRunner->runScript(pythonCode, m_currentCsvPath, m_currentCityName, m_currentTargetDate);
}

/**
 * @brief Obsługuje gotowy plik wykresu i odświeża sekcje UI.
 * @param chartPath Ścieżka do obrazu wygenerowanego przez skrypt.
 *
 * Statystyki tekstowe są już wyświetlone (przez onWeatherReady lub
 * onGenerateDayClicked), więc tutaj tylko ładujemy i skalujemy wykres.
 */
void MainWindow::onChartReady(const QString &chartPath)
{
    appendLog(QString("OK: Wykres wygenerowany: %1").arg(chartPath));

    m_currentChartPixmap = QPixmap(chartPath);
    if (m_currentChartPixmap.isNull()) {
        onError("Nie udało się załadować wygenerowanego wykresu.");
        return;
    }

    setGenerating(false);
    appendLog("Gotowe.");

    updateChartDisplay();
}

/**
 * @brief Centralny handler błędów pochodzących z modułów roboczych.
 * @param message Tekst błędu prezentowany użytkownikowi.
 */
void MainWindow::onError(const QString &message)
{
    appendLog(QString("BŁĄD: %1").arg(message));
    setGenerating(false);
    QMessageBox::critical(this, "Błąd", message);
}

/**
 * @brief Wysyła pytanie użytkownika do modelu AI w trybie streamingu.
 *
 * Metoda aktualizuje historię czatu, blokuje panel zapytań i uruchamia
 * asynchroniczne zapytanie o rekomendację pogodową.
 */
void MainWindow::onAskClicked()
{
    if (m_currentStatsSummary.isEmpty()) return;

    QString question = ui->chatInput->text().trimmed();
    if (question.isEmpty()) return;

    ui->chatHistory->append(QString("<b>Ty:</b> %1").arg(question.toHtmlEscaped()));
    ui->chatInput->clear();

    appendLog("Wysyłanie zapytania do modelu Ollama (streaming)...");
    m_statusSpinner->startSpinning();
    ui->chatGroup->setEnabled(false);

    m_streamingFirstChunk = true;

    QMetaObject::invokeMethod(m_ollamaClient, "askRecommendation",
                              Qt::QueuedConnection,
                              Q_ARG(QString, question),
                              Q_ARG(QString, m_currentStatsSummary));
}

/**
 * @brief Obsługuje pojedynczy chunk tekstu ze streamu Ollama.
 *
 * Przy pierwszym chunku dodaje nagłówek "Pogodynka AI:", kolejne chunki
 * dołączane są bezpośrednio, tworząc efekt "pisania na żywo".
 *
 * @param chunk Fragment odpowiedzi modelu.
 */
void MainWindow::onRecommendationChunk(const QString &chunk)
{
    if (m_streamingFirstChunk) {
        m_streamingFirstChunk = false;
        QTextCursor cursor = ui->chatHistory->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertHtml("<br><b>Pogodynka AI:</b> ");
        ui->chatHistory->setTextCursor(cursor);
    }

    QTextCursor cursor = ui->chatHistory->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(chunk);
    ui->chatHistory->setTextCursor(cursor);
    ui->chatHistory->ensureCursorVisible();
}

/**
 * @brief Obsługuje zakończenie odpowiedzi (streaming done lub non-streaming).
 *
 * Gdy text jest pusty — sygnał pochodzi z zakończenia streamu.
 * Gdy text jest niepusty — tryb non-streaming (fallback), wstawia cały tekst.
 *
 * @param text Treść (pusta przy streamingu, pełna przy non-streaming).
 */
void MainWindow::onRecommendationReady(const QString &text)
{
    m_statusSpinner->stopSpinning();
    ui->chatGroup->setEnabled(true);
    appendLog("OK: Odpowiedź gotowa.");

    QTextCursor cursor = ui->chatHistory->textCursor();
    cursor.movePosition(QTextCursor::End);

    if (!text.isEmpty()) {
        cursor.insertHtml("<br><b>Pogodynka AI:</b> ");
        cursor.insertFragment(QTextDocumentFragment::fromMarkdown(text));
    }

    cursor.insertHtml("<br>");
    ui->chatHistory->setTextCursor(cursor);
    ui->chatHistory->ensureCursorVisible();
}

/**
 * @brief Zapisuje aktualny wykres do pliku wybranego przez użytkownika.
 */
void MainWindow::onSaveChartClicked()
{
    if (m_currentChartPixmap.isNull()) {
        QMessageBox::information(this, "Brak wykresu", "Najpierw wygeneruj wykres.");
        return;
    }

    QString safeCity = m_currentCityName.trimmed();
    if (safeCity.isEmpty()) safeCity = "miasto";
    safeCity.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    const QString defaultName = QString("wykres_%1_%2.png")
        .arg(safeCity)
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

    QString outputPath = QFileDialog::getSaveFileName(
        this, "Zapisz wykres", QDir::home().filePath(defaultName),
        "Obrazy PNG (*.png);;Obrazy JPEG (*.jpg *.jpeg);;Obrazy BMP (*.bmp)"
    );
    if (outputPath.isEmpty()) return;

    if (!m_currentChartPixmap.save(outputPath)) {
        QMessageBox::warning(this, "Błąd zapisu", "Nie udało się zapisać pliku wykresu.");
        return;
    }
    appendLog(QString("OK: Wykres zapisany: %1").arg(outputPath));
}

/**
 * @brief Wyświetla okno z ostatnim wygenerowanym kodem Pythona.
 */
void MainWindow::onShowCodeClicked()
{
    if (m_lastGeneratedPythonCode.trimmed().isEmpty()) {
        QMessageBox::information(this, "Brak kodu", "Kod wykresu nie jest jeszcze dostępny.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Kod użyty do generowania wykresu");
    dialog.resize(860, 560);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QPlainTextEdit *codeView = new QPlainTextEdit(&dialog);
    codeView->setReadOnly(true);
    codeView->setPlainText(m_lastGeneratedPythonCode);
    codeView->setLineWrapMode(QPlainTextEdit::NoWrap);
    codeView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(codeView);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

/**
 * @brief Wyświetla pełną historię logów aplikacji w osobnym oknie.
 */
void MainWindow::onShowLogsClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Logi aplikacji");
    dialog.resize(860, 460);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QPlainTextEdit *logView = new QPlainTextEdit(&dialog);
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logView->setPlainText(m_logHistory.isEmpty() ? QString("Brak logów.") : m_logHistory.join('\n'));
    layout->addWidget(logView);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

/**
 * @brief Dodaje wpis do logu aplikacji i aktualizuje pasek statusu.
 * @param message Treść komunikatu.
 */

void MainWindow::appendLog(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    const QString fullMessage = QString("[%1] %2").arg(timestamp, message);
    m_logHistory.append(fullMessage);
    if (m_logHistory.size() > 1000) m_logHistory.removeFirst();
    m_statusLabel->setText(fullMessage);
    m_statusLabel->setToolTip(fullMessage);
}

/**
 * @brief Ustawia stan kontrolek podczas operacji generowania.
 * @param generating true gdy trwa operacja, false gdy UI ma wrócić do trybu interaktywnego.
 */
void MainWindow::setGenerating(bool generating)
{
    ui->generateBtn->setEnabled(!generating);
    ui->cityInput->setEnabled(!generating);
    ui->daysInput->setEnabled(!generating);
    ui->locateBtn->setEnabled(!generating);

    if (generating) {
        m_statusSpinner->startSpinning();
    } else {
        m_statusSpinner->stopSpinning();
    }

    bool hasData = !m_currentCsvPath.isEmpty();
    ui->daySelector->setEnabled(!generating && hasData);
    ui->generateDayBtn->setEnabled(!generating && hasData);
    ui->saveChartBtn->setEnabled(!generating && !m_currentChartPixmap.isNull());
    ui->showCodeBtn->setEnabled(!generating && !m_lastGeneratedPythonCode.trimmed().isEmpty());

    if (!generating && m_statusLabel && m_statusLabel->text().isEmpty()) {
        m_statusLabel->setText("[--:--:--] Gotowe");
    }
}

/**
 * @brief Obsługuje zmianę rozmiaru okna i odświeża skalę wykresu.
 * @param event Zdarzenie resize od Qt.
 */
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateChartDisplay();
}

/**
 * @brief Skaluje aktualny wykres do szerokości obszaru przewijania.
 */
void MainWindow::updateChartDisplay()
{
    if (m_currentChartPixmap.isNull()) return;

    int availableWidth = ui->scrollArea->viewport()->width();
    if (availableWidth <= 0) return;

    int targetWidth = availableWidth - 25;
    if (targetWidth <= 0) targetWidth = availableWidth;

    QPixmap scaledPixmap = m_currentChartPixmap.scaledToWidth(
        targetWidth, Qt::SmoothTransformation);
    ui->chartLabel->setPixmap(scaledPixmap);
    ui->chartLabel->setAlignment(Qt::AlignCenter);
}

/**
 * @brief Oblicza statystyki pogodowe i wyświetla je w statsLabel.
 * @param csvPath Ścieżka do pliku CSV z danymi godzinowymi.
 * @param targetDate Opcjonalna data filtrowania; pusty string oznacza cały okres.
 *
 * Funkcja:
 * - agreguje temperaturę, opady, wilgotność i wiatr,
 * - wylicza kierunek dominującego wiatru,
 * - dołącza dane wschodu/zachodu słońca,
 * - prezentuje PM2.5, jeśli zostało pobrane,
 * - buduje podsumowanie tekstowe wykorzystywane w module czatu.
 */
void MainWindow::calculateAndDisplayStats(const QString &csvPath, const QString &targetDate)
{
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    in.readLine(); // nagłówek

    double sumTemp = 0.0, minTemp = 999.0, maxTemp = -999.0;
    double sumPrecip = 0.0, sumHum = 0.0, sumWind = 0.0;
    double sumWindU = 0.0, sumWindV = 0.0;
    int count = 0;

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty()) continue;
        QStringList parts = line.split(',');
        if (parts.size() < 5) continue;

        QString dt = parts[0];
        if (!targetDate.isEmpty() && !dt.startsWith(targetDate)) continue;

        double temp   = parts[1].toDouble();
        double precip = parts[2].toDouble();
        double hum    = parts[3].toDouble();
        double wind   = parts[4].toDouble();
        double windDir = (parts.size() > 5) ? parts[5].toDouble() : 0.0;

        sumTemp += temp;
        if (temp < minTemp) minTemp = temp;
        if (temp > maxTemp) maxTemp = temp;
        sumPrecip += precip;
        sumHum    += hum;
        sumWind   += wind;
        sumWindU  += sin(windDir * M_PI / 180.0);
        sumWindV  += cos(windDir * M_PI / 180.0);
        count++;
    }
    file.close();

    if (count == 0) {
        ui->statsLabel->clear();
        m_currentStatsSummary.clear();
        ui->chatGroup->setEnabled(false);
        return;
    }

    double avgTemp = sumTemp / count;
    double avgHum  = sumHum  / count;
    double avgWind = sumWind / count;

    double avgWindDirDeg = atan2(sumWindU, sumWindV) * 180.0 / M_PI;
    if (avgWindDirDeg < 0) avgWindDirDeg += 360.0;
    const char *dirs[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE",
                          "S","SSW","SW","WSW","W","WNW","NW","NNW"};
    int dirIndex = static_cast<int>((avgWindDirDeg / 22.5) + 0.5);
    QString windDirStr = dirs[dirIndex % 16];

    applyTemperatureTheme(avgTemp);

    // Wschód/zachód słońca z daily JSON
    QString sunriseStr = "--:--", sunsetStr = "--:--";
    QString dailyPath = QCoreApplication::applicationDirPath() + "/weather_data_daily.json";
    QFile dailyFile(dailyPath);
    if (dailyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonDocument dailyDoc = QJsonDocument::fromJson(dailyFile.readAll());
        QJsonObject daily = dailyDoc.object();
        QJsonArray times    = daily.value("time").toArray();
        QJsonArray sunrises = daily.value("sunrise").toArray();
        QJsonArray sunsets  = daily.value("sunset").toArray();
        if (!targetDate.isEmpty()) {
            for (int i = 0; i < times.size(); ++i) {
                if (times[i].toString() == targetDate) {
                    sunriseStr = sunrises[i].toString().split('T').last();
                    sunsetStr  = sunsets[i].toString().split('T').last();
                    break;
                }
            }
        } else if (!sunrises.isEmpty()) {
            sunriseStr = sunrises[0].toString().split('T').last() + " (dziś)";
            sunsetStr  = sunsets[0].toString().split('T').last()  + " (dziś)";
        }
        dailyFile.close();
    }

    QString aqiHtml;
    if (m_currentPm25 >= 0.0) {
        QString aqiCategory, aqiColor;
        if      (m_currentPm25 < 10.0) { aqiCategory = "Bardzo dobra";  aqiColor = "#27ae60"; }
        else if (m_currentPm25 < 20.0) { aqiCategory = "Dobra";          aqiColor = "#2ecc71"; }
        else if (m_currentPm25 < 25.0) { aqiCategory = "Umiarkowana";    aqiColor = "#f39c12"; }
        else if (m_currentPm25 < 50.0) { aqiCategory = "Dostateczna";    aqiColor = "#e67e22"; }
        else                            { aqiCategory = "Zła";            aqiColor = "#e74c3c"; }

        aqiHtml = QString(
            "<tr><td colspan=\"5\" style=\"padding-top:4px; border-top:1px solid #555; text-align:center;\">"
            "<b>Jakość powietrza PM2.5:</b> "
            "<span style=\"color:%1;\">%2 µg/m³ — %3</span>"
            "</td></tr>"
        ).arg(aqiColor).arg(m_currentPm25, 0, 'f', 1).arg(aqiCategory);
    }

    QString titleStr = targetDate.isEmpty()
        ? "Średnia z całego okresu"
        : QString("Dzień: %1").arg(targetDate);

    const auto temperatureStyle = [](double value, const char* neutralColor) -> QString {
        if (value > 20.0) {
            return "color:#FF8C00;"; // Warm temperature: orange
        }
        if (value < 10.0) {
            return "color:#4DA3FF;"; // Cold temperature: blue
        }
        return QString("color:%1;").arg(neutralColor);
    };

    const QString avgTempStyle = temperatureStyle(avgTemp, "#FFFFFF");
    const QString minTempStyle = temperatureStyle(minTemp, "#888");
    const QString maxTempStyle = temperatureStyle(maxTemp, "#888");

    QString statsText = QString(
        "<html><head><style>"
        "table { text-align:center; width:94%; margin:0 auto; border-collapse:collapse; table-layout:fixed; }"
        "td { padding:4px 6px; vertical-align:top; text-align:center; }"
        ".sep { border-right:1px solid #555; }"
        ".main { font-size:10pt; }"
        ".sub { font-size:9pt; color:#888; }"
        "</style></head><body>"
        "<div style=\"text-align:center;font-size:12pt;margin-bottom:4px;\"><b>%1</b></div>"
        "<table cellspacing=\"0\" cellpadding=\"0\">"
        "<tr>"
        "<td class=\"sep\"><b>Temperatura</b><br/>"
        "  <span class=\"main\" style=\"%11\">Śr: %2°C</span><br/>"
        "  <span class=\"sub\" style=\"%12\">Min: %3°C</span><br/>"
        "  <span class=\"sub\" style=\"%13\">Max: %4°C</span></td>"
        "<td class=\"sep\"><b>Opady</b><br/><span class=\"main\">Suma: %5 mm</span></td>"
        "<td class=\"sep\"><b>Wilgotność</b><br/><span class=\"main\">Śr: %6%%</span></td>"
        "<td class=\"sep\"><b>Wiatr</b><br/><span class=\"main\">Śr: %7 km/h</span><br/>"
        "  <span class=\"sub\">Kier.: %8</span></td>"
        "<td><b>Słońce</b><br/>"
        "  <span class=\"main\" style=\"color:#FF8C00;\">Wschód: %9</span><br/>"
        "  <span class=\"main\" style=\"color:#6A5ACD;\">Zachód: %10</span></td>"
        "</tr>"
        "%14"   // <-- wiersz AQI (pusty jeśli brak danych)
        "</table>"
        "</body></html>"
    )
    .arg(titleStr)
    .arg(avgTemp,  0, 'f', 1).arg(minTemp, 0, 'f', 1).arg(maxTemp, 0, 'f', 1)
    .arg(sumPrecip, 0, 'f', 1).arg(avgHum, 0, 'f', 0)
    .arg(avgWind,  0, 'f', 1).arg(windDirStr)
    .arg(sunriseStr).arg(sunsetStr)
    .arg(avgTempStyle).arg(minTempStyle).arg(maxTempStyle)
    .arg(aqiHtml);

    ui->statsLabel->setText(statsText);

    m_currentStatsSummary = QString(
                                "Dla miejscowości %1, średnia temp: %2°C (min: %3, max: %4). "
                                "Opady: %5 mm. Wiatr: %6 km/h. Słońce od %7 do %8."
                                "%9"
                                )
    .arg(m_currentCityName)
    .arg(avgTemp,  0,'f',1).arg(minTemp,0,'f',1).arg(maxTemp,0,'f',1)
    .arg(sumPrecip,0,'f',1).arg(avgWind,0,'f',1)
    .arg(sunriseStr).arg(sunsetStr)
    .arg(m_currentPm25 >= 0
        ? QString(" PM2.5: %1 µg/m³.").arg(m_currentPm25, 0,'f',1)
        : QString());

    ui->chatGroup->setEnabled(true);
}
