# Pogodynka AI

Desktopowa aplikacja w C++/Qt6 do pobierania prognozy pogody i generowania wykresu temperatury przy pomocy lokalnego modelu językowego (Ollama) oraz Pythona (matplotlib).

## Co robi aplikacja

1. Pobiera dane pogodowe z Open-Meteo dla wybranego miasta i zapisuje informacje w pliku weather_data.csv .
2. Wysyła prompt do lokalnego modelu (Ollama), aby wygenerować skrypt Python z wykresem.
3. Uruchamia skrypt i wyświetla gotowy wykres temperatury.
4. Odpowiada na pytania.

Operacje sieciowe i komunikacja z modelem działają w osobnych wątkach, więc GUI pozostaje responsywne.

## Wymagania

- Qt 6.x (Widgets, Network)
- CMake >= 3.16
- Kompilator C++17 (MinGW/MSVC/GCC)
- Python 3.x
- Ollama z pobranym modelem (np. llama3.2)

## Instalacja zależności

1) Python
pip install -r requirements.txt

2) Ollama
ollama pull gemma4:e4b
ollama serve

## Autorzy

D. D. 
J. B.
M. C.

Projekt edukacyjny (JPO — Jezyki i Programowanie Obiektowe).
