/**
 * @file test_ollamaclient.cpp
 * @brief Testy jednostkowe modułu komunikacji z Ollama.
 */

#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>

#include "../ollamaclient.h"

/**
 * @class TestOllamaClient
 * @brief Testy jednostkowe dla OllamaClient — budowanie JSON i ekstrakcja kodu.
 */
class TestOllamaClient : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Test budowania poprawnego JSON-a zapytania.
     */
    void testBuildRequestJson()
    {
        OllamaClient client;
        QByteArray json = client.buildRequestJson("/path/to/data.csv", "Warszawa");

        QJsonDocument doc = QJsonDocument::fromJson(json);
        QVERIFY(!doc.isNull());
        QVERIFY(doc.isObject());

        QJsonObject obj = doc.object();
        QVERIFY(obj.contains("model"));
        QVERIFY(obj.contains("prompt"));
        QVERIFY(obj.contains("system"));
        QVERIFY(obj.contains("stream"));

        // stream powinien być false
        QCOMPARE(obj.value("stream").toBool(), false);

        // prompt powinien zawierać ścieżkę CSV i nazwę miasta
        QString prompt = obj.value("prompt").toString();
        QVERIFY(prompt.contains("/path/to/data.csv"));
        QVERIFY(prompt.contains("Warszawa"));

        // system prompt powinien wymuszać czysty kod
        QString system = obj.value("system").toString();
        QVERIFY(system.contains("Python"));
    }

    /**
     * @brief Test ekstrakcji kodu z czystej odpowiedzi (bez markdown).
     */
    void testExtractPythonCodeClean()
    {
        QString raw = "import matplotlib.pyplot as plt\nimport pandas as pd\n\ndf = pd.read_csv('data.csv')\nplt.plot(df['temperature'])\nplt.savefig('chart.png')\n";

        QString code = OllamaClient::extractPythonCode(raw);
        QVERIFY(code.startsWith("import matplotlib"));
        QVERIFY(code.contains("plt.savefig"));
    }

    /**
     * @brief Test ekstrakcji kodu z blokiem markdown ```python.
     */
    void testExtractPythonCodeWithMarkdown()
    {
        QString raw = "Here is the code:\n\n```python\nimport matplotlib.pyplot as plt\nprint('hello')\n```\n\nHope this helps!";

        QString code = OllamaClient::extractPythonCode(raw);
        QVERIFY(code.startsWith("import matplotlib"));
        QVERIFY(code.contains("print('hello')"));
        QVERIFY(!code.contains("```"));
        QVERIFY(!code.contains("Hope this helps"));
    }

    /**
     * @brief Test ekstrakcji kodu z blokiem markdown ``` (bez slowa python).
     */
    void testExtractPythonCodeWithGenericMarkdown()
    {
        QString raw = "```\nimport os\nprint(os.getcwd())\n```";

        QString code = OllamaClient::extractPythonCode(raw);
        QVERIFY(code.startsWith("import os"));
        QVERIFY(!code.contains("```"));
    }

    /**
     * @brief Test ekstrakcji z pustej odpowiedzi.
     */
    void testExtractPythonCodeEmpty()
    {
        QString code = OllamaClient::extractPythonCode("");
        QVERIFY(code.isEmpty());
    }

    /**
     * @brief Test ekstrakcji z odpowiedzi zawierającej tylko tekst (brak kodu).
     */
    void testExtractPythonCodeNoCode()
    {
        QString raw = "I cannot generate code for this request.";
        QString code = OllamaClient::extractPythonCode(raw);
        // Powinno zwrócić cokolwiek (nie jest puste), ale nie jest to poprawny Python
        // — walidacja poprawności kodu nie jest zadaniem extractPythonCode
        QVERIFY(!code.isEmpty());
    }
};

int runTestOllamaClient(int argc, char *argv[])
{
    TestOllamaClient tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_ollamaclient.moc"
