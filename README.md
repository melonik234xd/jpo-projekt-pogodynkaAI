# Pogodynka AI

Desktopowa aplikacja w C++/Qt6 do pobierania prognozy pogody i generowania wykresu temperatury przy pomocy lokalnego modelu językowego (Ollama) oraz Pythona (matplotlib).

## Co robi aplikacja

1. Pobiera dane pogodowe z Open-Meteo dla wybranego miasta.
2. Wysyła prompt do lokalnego modelu (Ollama), aby wygenerować skrypt Python z wykresem.
3. Uruchamia skrypt i wyświetla gotowy wykres temperatury.

Operacje sieciowe i komunikacja z modelem działają w osobnych wątkach, więc GUI pozostaje responsywne.

## Wymagania

- Qt 6.x (Widgets, Network)
- CMake >= 3.16
- Kompilator C++17 (MinGW/MSVC/GCC)
- Python 3.x
- Ollama z pobranym modelem (np. `llama3.2`)

## Instalacja zależności

### 1) Python

```bash
pip install -r requirements.txt
```

### 2) Ollama

```bash
ollama pull llama3.2
ollama serve
```

## Budowanie

```bash
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=<sciezka_do_Qt6>
cmake --build .
```

### Budowanie z testami

```bash
cmake .. -DBUILD_TESTS=ON
cmake --build .
ctest
```

## Uruchomienie

1. Upewnij się, że działa Ollama (`ollama serve`).
2. Uruchom aplikację (`./jpo-pogodynka`).
3. Wpisz nazwę miasta i zakres prognozy.
4. Kliknij przycisk generowania wykresu.

## Tryb offline

Przy braku internetu aplikacja próbuje użyć ostatnio zapisanych danych (`weather_data.csv`).
Jeśli Ollama nie jest dostępna, aplikacja pokazuje komunikat o błędzie.

## Testy

Projekt zawiera testy jednostkowe w katalogu `tests/`:

- `test_weatherparser.cpp`
- `test_ollamaclient.cpp`
- `test_scriptrunner.cpp`
- runner: `test_main.cpp`

Uruchamianie:

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --target jpo-pogodynka-test
ctest --test-dir build --output-on-failure
```

## Struktura projektu

```text
jpo-pogodynka/
|- CMakeLists.txt
|- main.cpp
|- mainwindow.cpp
|- mainwindow.h
|- mainwindow.ui
|- weatherapiclient.cpp
|- weatherapiclient.h
|- ollamaclient.cpp
|- ollamaclient.h
|- scriptrunner.cpp
|- scriptrunner.h
|- requirements.txt
|- tests/
|  |- test_weatherparser.cpp
|  |- test_ollamaclient.cpp
|  |- test_scriptrunner.cpp
|- Doxyfile
|- README.txt
`- README.md
```

## Do zrobienia / poprawki

- [to fix] wyswietlanie dodatkowych info w sekcji nie na pismie i moze wiecej informacji
- [todo] dodanie mozliwosci zapytania modelu o informacje/rekomendacje pogodowe
- [to fix] logi pod przycisk nie na srodku menu
- [to fix] brak setFullWindow() czy jakos tak przy wygenerowaniu wykresu
- [todo] config file z ustawieniami w apce 
- [todo] poprawa GUI ???
- [todo] mozliwosc wybrania wykresu slupkowego dla temperatury
- [to fix] podział na dzień i noc + informacje o wschodzie i zachodzie słońca 

## Autor

Projekt edukacyjny (JPO — Jezyki i Programowanie Obiektowe).
