/**
 * @file test_scriptrunner.cpp
 * @brief Testy jednostkowe modułu wykonawczego skryptów.
 */

#include <QTest>
#include <QFile>
#include <QSignalSpy>
#include <QTextStream>

#include "../scriptrunner.h"

/**
 * @class TestScriptRunner
 * @brief Testy jednostkowe dla ScriptRunner — zapis skryptu i ścieżki.
 */
class TestScriptRunner : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Test, czy ścieżka do skryptu ma rozszerzenie .py.
     */
    void testScriptPathExtension()
    {
        ScriptRunner runner;
        QString path = runner.scriptPath();
        QVERIFY(path.endsWith(".py"));
    }

    /**
     * @brief Test, czy ścieżka do wykresu ma rozszerzenie .png.
     */
    void testChartPathExtension()
    {
        ScriptRunner runner;
        QString path = runner.chartPath();
        QVERIFY(path.endsWith(".png"));
    }

    /**
     * @brief Test, czy skrypt jest poprawnie zapisywany do pliku.
     */
    void testScriptFileSaved()
    {
        ScriptRunner runner;
        QSignalSpy errorSpy(&runner, &ScriptRunner::errorOccurred);
        QSignalSpy chartSpy(&runner, &ScriptRunner::chartReady);
        QString testCode = "print('hello from test')";

        // Uruchom skrypt — nawet jeśli Python nie jest dostępny,
        // plik powinien zostać zapisany
        runner.runScript(testCode);

        // Domknij test po zakończeniu procesu, aby nie zostawiać aktywnego QProcess.
        if (errorSpy.isEmpty() && chartSpy.isEmpty()) {
            QVERIFY(errorSpy.wait(10000) || chartSpy.wait(10000));
        }

        QFile file(runner.scriptPath());
        QVERIFY(file.exists());

        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QTextStream in(&file);
        QString content = in.readAll();
        QCOMPARE(content, testCode);
    }

    /**
     * @brief Test sygnału errorOccurred dla błędnego skryptu Python.
     */
    void testErrorSignalOnBadScript()
    {
        ScriptRunner runner;
        QSignalSpy errorSpy(&runner, &ScriptRunner::errorOccurred);

        // Skrypt z błędem składniowym
        runner.runScript("this is not valid python !!!");

        // Czekaj na zakończenie (max 10 sekund)
        if (errorSpy.isEmpty()) {
            QVERIFY(errorSpy.wait(10000));
        }

        QVERIFY(errorSpy.count() > 0);
    }
};

int runTestScriptRunner(int argc, char *argv[])
{
    TestScriptRunner tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_scriptrunner.moc"
