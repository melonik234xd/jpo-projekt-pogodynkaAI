/**
 * @file main.cpp
 * @brief Punkt wejścia aplikacji Pogodynka AI.
 *
 * Tworzy instancję QApplication i główne okno MainWindow.
 */

#include "mainwindow.h"

#include <QApplication>

/**
 * @brief Główna funkcja programu.
 * @param argc Liczba argumentów wiersza poleceń.
 * @param argv Tablica argumentów wiersza poleceń.
 * @return Kod zakończenia aplikacji.
 */
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("Pogodynka AI");
    a.setApplicationVersion("1.0");

    MainWindow w;
    w.show();
    return QCoreApplication::exec();
}
