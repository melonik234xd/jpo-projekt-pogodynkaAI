/**
 * @file scriptrunner.cpp
 * @brief Implementacja silnika wykonawczego skryptów Python.
 */

#include "scriptrunner.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

/**
 * @brief Inicjalizuje ścieżki robocze i podpina obsługę zakończenia procesu.
 * @param parent Obiekt nadrzędny Qt.
 */
ScriptRunner::ScriptRunner(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_usedFallback(false)
{
    m_workingDir = QCoreApplication::applicationDirPath();
    m_scriptPath = m_workingDir + "/plot_weather.py";
    m_chartPath = m_workingDir + "/chart.png";

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ScriptRunner::onProcessFinished);
}

/**
 * @brief Zapisuje kod Pythona do pliku i uruchamia interpretację.
 * @param pythonCode Kod źródłowy do wykonania.
 * @param csvPath Ścieżka do CSV używana przez mechanizm fallback.
 * @param cityName Nazwa miasta używana przez fallback.
 * @param targetDate Opcjonalna data filtrowania dla fallback.
 */
void ScriptRunner::runScript(const QString &pythonCode, const QString &csvPath, const QString &cityName, const QString &targetDate)
{
    // Zapisz dane do mechanizmu awaryjnego
    m_csvPath = csvPath;
    m_cityName = cityName;
    m_targetDate = targetDate;
    m_usedFallback = false;

    // Zapisz kod do pliku .py
    QFile file(m_scriptPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit errorOccurred(
            QString("Nie można zapisać skryptu do pliku: %1").arg(m_scriptPath));
        return;
    }

    QTextStream out(&file);
    out << pythonCode;
    file.close();

    // Usuń stary wykres, jeśli istnieje
    if (QFile::exists(m_chartPath)) {
        QFile::remove(m_chartPath);
    }

    // Uruchom interpreter Python
    m_process->setWorkingDirectory(m_workingDir);
    m_process->start("python", QStringList() << m_scriptPath);

    if (!m_process->waitForStarted(5000)) {
        emit errorOccurred(
            "Nie można uruchomić interpretera Python. "
            "Upewnij się, że Python jest zainstalowany i dostępny w PATH.");
    }
}

/**
 * @brief Zwraca ścieżkę docelową pliku wykresu.
 * @return Absolutna ścieżka do chart.png.
 */
QString ScriptRunner::chartPath() const
{
    return m_chartPath;
}

/**
 * @brief Zwraca ścieżkę zapisywanego pliku skryptu.
 * @return Absolutna ścieżka do plot_weather.py.
 */
QString ScriptRunner::scriptPath() const
{
    return m_scriptPath;
}

/**
 * @brief Generuje zapasowy skrypt Pythona do rysowania wykresu.
 * @return Kod źródłowy fallbacku gotowy do zapisania i uruchomienia.
 */
QString ScriptRunner::generateFallbackScript() const
{
    QString filterLogic;
    if (!m_targetDate.isEmpty()) {
        filterLogic = QString(
            "target = pd.to_datetime('%1')\n"
            "df = df[(df['datetime'] >= target - pd.Timedelta(hours=12)) & (df['datetime'] <= target + pd.Timedelta(hours=36))]\n"
        ).arg(m_targetDate);
    }

    return QString(
        "import matplotlib\n"
        "matplotlib.use('Agg')\n"
        "import matplotlib.pyplot as plt\n"
        "import matplotlib.dates as mdates\n"
        "import pandas as pd\n"
        "\n"
        "df = pd.read_csv('%1')\n"
        "df['datetime'] = pd.to_datetime(df['datetime'])\n"
        "%3\n"
        "plt.style.use('ggplot')\n"
        "fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 10), facecolor='#f8f9fa')\n"
        "fig.suptitle('Szczegóły pogody - %2', fontsize=18, fontweight='bold', color='#333333', y=0.96)\n"
        "\n"
        "ax1.plot(df['datetime'], df['temperature'], color='#e74c3c', linewidth=2.5, marker='o', markersize=3)\n"
        "ax1.fill_between(df['datetime'], df['temperature'], df['temperature'].min() - 2, alpha=0.15, color='#e74c3c')\n"
        "ax1.set_title('Temperatura', fontsize=14, pad=10)\n"
        "ax1.set_ylabel('C', fontsize=12)\n"
        "ax1.grid(True, alpha=0.4, linestyle='--')\n"
        "ax1.xaxis.set_major_formatter(mdates.DateFormatter('%d.%m %H:00'))\n"
        "ax1.tick_params(axis='x', rotation=45)\n"
        "\n"
        "ax2.bar(df['datetime'], df['precipitation'], color='#3498db', width=0.04, alpha=0.8, edgecolor='#2980b9')\n"
        "ax2.set_title('Opady', fontsize=14, pad=10)\n"
        "ax2.set_ylabel('mm', fontsize=12)\n"
        "ax2.grid(True, alpha=0.4, linestyle='--')\n"
        "ax2.xaxis.set_major_formatter(mdates.DateFormatter('%d.%m %H:00'))\n"
        "ax2.tick_params(axis='x', rotation=45)\n"
        "\n"
        "plt.tight_layout()\n"
        "plt.subplots_adjust(top=0.9, hspace=0.4)\n"
        "plt.savefig('chart.png', dpi=100, bbox_inches='tight')\n"
    ).arg(m_csvPath).arg(m_cityName).arg(filterLogic);
}

/**
 * @brief Obsługuje zakończenie procesu Python i emituje wynik działania.
 * @param exitCode Kod zakończenia procesu.
 * @param exitStatus Status zakończenia procesu.
 */
void ScriptRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        QString stderrOutput = m_process->readAllStandardError();

        // Jeśli skrypt AI zawiódł i mamy dane do fallbacku — spróbuj ponownie
        if (!m_usedFallback && !m_csvPath.isEmpty()) {
            m_usedFallback = true;

            // Zapisz skrypt awaryjny
            QFile file(m_scriptPath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << generateFallbackScript();
                file.close();

                // Usuń stary wykres
                if (QFile::exists(m_chartPath)) {
                    QFile::remove(m_chartPath);
                }

                // Uruchom ponownie
                m_process->setWorkingDirectory(m_workingDir);
                m_process->start("python", QStringList() << m_scriptPath);
                if (!m_process->waitForStarted(5000)) {
                    emit errorOccurred("Nie można uruchomić interpretera Python (fallback).");
                }
                return; // czekaj na wynik fallbacku
            }
        }

        emit errorOccurred(
            QString("Skrypt Python zakończył się błędem (kod %1):\n%2")
                .arg(exitCode)
                .arg(stderrOutput));
        return;
    }

    // Sprawdź, czy plik wykresu został wygenerowany
    if (!QFileInfo::exists(m_chartPath)) {
        // Spróbuj fallback
        if (!m_usedFallback && !m_csvPath.isEmpty()) {
            m_usedFallback = true;
            QFile file(m_scriptPath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << generateFallbackScript();
                file.close();
                m_process->setWorkingDirectory(m_workingDir);
                m_process->start("python", QStringList() << m_scriptPath);
                if (!m_process->waitForStarted(5000)) {
                    emit errorOccurred("Nie można uruchomić interpretera Python (fallback).");
                }
                return;
            }
        }

        QString stdout_str = m_process->readAllStandardOutput();
        QString stderr_str = m_process->readAllStandardError();
        emit errorOccurred(
            QString("Skrypt zakończył się pomyślnie, ale plik chart.png nie został "
                    "wygenerowany.\nStdout: %1\nStderr: %2")
                .arg(stdout_str)
                .arg(stderr_str));
        return;
    }

    emit chartReady(m_chartPath);
}
