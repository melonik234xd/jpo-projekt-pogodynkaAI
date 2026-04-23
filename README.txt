===============================================================================
                                POGODYNKA AI
===============================================================================

Desktopowa aplikacja w C++/Qt6 do pobierania prognozy pogody i generowania 
wykresów przy pomocy lokalnego modelu językowego (Ollama) oraz Pythona 
(matplotlib, pandas).

-------------------------------------------------------------------------------
CO ROBI APLIKACJA
-------------------------------------------------------------------------------

1. AUTOMATYCZNA LOKALIZACJA I PODPOWIEDZI: Przy uruchomieniu aplikacja 
   korzysta z API (ip-api.com), aby na podstawie adresu IP automatycznie 
   wykryć miasto użytkownika. Pole wyszukiwania posiada funkcję 
   autouzupełniania z wbudowanej bazy popularnych miast.

2. POBIERANIE SZCZEGÓŁOWYCH STATYSTYK: Pobiera dane meteorologiczne z 
   Open-Meteo (temperatura, opady, wilgotność, siła wiatru, jakość powietrza 
   PM2.5, wschód/zachód słońca) i zapisuje je w pliku weather_data.csv.

3. GENEROWANIE WYKRESÓW PRZEZ AI: Wysyła prompt do modelu Ollama, aby 
   wygenerować skrypt Python. Aplikacja uruchamia skrypt i wyświetla gotowy 
   wykres (liniowy temperatury i słupkowy opadów) bezpośrednio w oknie.

4. INTELIGENTNY ASYSTENT (CZAT): Zintegrowany czat pozwala na zadawanie 
   naturalnych pytań (np. "Kiedy będzie padać?", "Czy wziąć parasol?"). 
   Model odpowiada na podstawie bieżących statystyk i pliku CSV.

WIELOWĄTKOWOŚĆ: Operacje sieciowe i komunikacja z AI działają w osobnych 
wątkach, dzięki czemu interfejs (GUI) pozostaje zawsze responsywny.

-------------------------------------------------------------------------------
WYMAGANIA
-------------------------------------------------------------------------------

- Qt 6.x (Widgets, Network)
- CMake >= 3.16
- Kompilator C++17 (MinGW/MSVC/GCC)
- Python 3.x
- Ollama z modelem gemma4:e4b
- Doxygen (do dokumentacji)

-------------------------------------------------------------------------------
INSTALACJA ZALEŻNOŚCI
-------------------------------------------------------------------------------

1) PYTHON:
   Wpisz w konsoli: pip install -r requirements.txt

2) OLLAMA:
   Pobierz model: ollama pull gemma4:e4b
   Uruchom serwer: ollama serve

-------------------------------------------------------------------------------
URUCHOMIENIE W ŚRODOWISKU QT CREATOR (Zalecane)
-------------------------------------------------------------------------------

1. Upewnij się, że serwer Ollama działa (ollama serve).
2. Otwórz Qt Creator.
3. Wybierz File -> Open File or Project... i wskaż plik CMakeLists.txt.
4. W oknie Configure Project zaznacz odpowiedni Kit (np. MinGW 64-bit) 
   i kliknij Configure.
5. Kliknij zielony trójkąt "Run" (lub Ctrl+R), aby uruchomić program.
6. Wpisz miasto, zakres dni i kliknij "Generuj wykres".

-------------------------------------------------------------------------------
TESTY JEDNOSTKOWE
-------------------------------------------------------------------------------

Projekt zawiera testy: test_weatherparser, test_ollamaclient, test_scriptrunner.

URUCHAMIANIE TESTÓW W QT CREATOR:
1. Przejdź do zakładki "Projects" (ikona klucza).
2. W "Build Settings" -> "CMake" upewnij się, że flaga -DBUILD_TESTS ma 
   wartość ON. Kliknij "Apply Configuration Changes".
3. Nad zielonym przyciskiem "Run" (ikona monitora) zmień "Run configuration" 
   z jpo-pogodynka na jpo-pogodynka-test.
4. Kliknij zielony trójkąt "Run" (Ctrl+R). Wyniki pojawią się w konsoli 
   "Application Output".

-------------------------------------------------------------------------------
TRYB OFFLINE
-------------------------------------------------------------------------------

Przy braku internetu aplikacja próbuje użyć ostatnich danych z weather_data.csv.
Jeśli Ollama nie jest dostępna, program wyświetli stosowny komunikat.

-------------------------------------------------------------------------------
STRUKTURA PROJEKTU
-------------------------------------------------------------------------------

jpo-pogodynka/
|- assets/             (Zdjęcia i zrzuty ekranu)
|- tests/              (Testy jednostkowe)
|- CMakeLists.txt      (Konfiguracja projektu)
|- Doxyfile            (Dokumentacja)
|- README.md / .txt    (Dokumentacja użytkownika)
|- requirements.txt    (Biblioteki Python)
|- *.cpp / *.h         (Kod źródłowy aplikacji)
`- *.ui                (Interfejs graficzny)

-------------------------------------------------------------------------------
AUTORZY
-------------------------------------------------------------------------------

- D. D.
- J. B.
- M. C.

Projekt edukacyjny (JPO - Języki i Programowanie Obiektowe).
===============================================================================
