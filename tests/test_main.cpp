/**
 * @file test_main.cpp
 * @brief Wspólny runner uruchamiający wszystkie testy jednostkowe.
 */

#include <QCoreApplication>

int runTestWeatherParser(int argc, char *argv[]);
int runTestOllamaClient(int argc, char *argv[]);
int runTestScriptRunner(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    int status = 0;
    status |= runTestWeatherParser(argc, argv);
    status |= runTestOllamaClient(argc, argv);
    status |= runTestScriptRunner(argc, argv);
    return status;
}
