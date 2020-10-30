#include <algorithm>
#include <tuple>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <numeric>
#include <cmath>
#include <utility>
#include <functional>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double EPSILON = 1e-6;

template<typename C, typename T>
auto insert_in_container(C& c, T&& t) ->
decltype(c.push_back(std::forward<T>(t)), void()) {
    c.push_back(std::forward<T>(t));
}

template<typename C, typename T>
auto insert_in_container(C& c, T&& t) ->
decltype(c.insert(std::forward<T>(t)), void()) {
    c.insert(std::forward<T>(t));
}

template <typename Container>
auto its_and_idx(Container&& container) {
    return std::tuple{ std::begin(container), std::end(container), 0 };
}

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

struct Document {
    Document() = default;
    Document(int id_, double relevance_, int rating_) {
        id = id_;
        relevance = relevance_;
        rating = rating_;
    }

    int id = 0;
    double relevance = 0.0;
    int rating = 0;
};


class SearchServer {
public:
    SearchServer() = default;

    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stopWords) {
        SetStopWords(stopWords);
    }

    explicit SearchServer(const string& stopWordsText) {
        SetStopWords(stopWordsText);
    }

    template <typename StringContainer>
    void SetStopWords(const StringContainer& stopWords) {
        for (const auto& stopWord : stopWords) {
            if (!isValidWord(stopWord)) {
                throw invalid_argument("Bad stop word");
            }
            m_stopWords.emplace(stopWord);
        }
    }

    void SetStopWords(const string& stopWordsText) {
        set<string> temp;
        split(temp, stopWordsText);
        SetStopWords(temp);
    }

    void AddDocument(int documentId, const string& document, DocumentStatus status, const vector<int>& ratings) {
        if (documentId < 0 || documents_.find(documentId) != documents_.end()) {
            throw invalid_argument("Bad document id");
        }
        vector<string> words;
        if (!splitIntoWordsNoStop(document, words)) {
            throw invalid_argument("Bad document data");
        };
        documents_.emplace(documentId, DocumentData{ computeAverageRating(ratings), status, words });
        document_ids_.push_back(documentId);
        calculateTermFrequency(documentId);
    }

    template <typename DocumentPredicate>
    vector<Document> FindTopDocuments(const string& rawQuery, const DocumentPredicate& document_predicate) const {
        Query query;
        if (!parseQuery(rawQuery, query)) {
            throw invalid_argument("Bad query");
        }
        vector<Document> matched_documents = findAllDocuments(query, document_predicate);

        sort(
            matched_documents.begin(),
            matched_documents.end(),
            [](const Document& lhs, const Document& rhs) {
            return lhs.relevance > rhs.relevance || (abs(lhs.relevance - rhs.relevance) < EPSILON && lhs.rating > rhs.rating);
        }
        );
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;
    }

    vector<Document> FindTopDocuments(const string& rawQuery, DocumentStatus status) const {
        return FindTopDocuments(rawQuery, [status](int document_id, DocumentStatus document_status, int rating) {
            return document_status == status;
        });
    }

    vector<Document> FindTopDocuments(const string& rawQuery) const {
        return FindTopDocuments(rawQuery, DocumentStatus::ACTUAL);
    }

    int GetDocumentCount() const {
        return documents_.size();
    }

    int GetDocumentId(int index) const {
        if (index >= 0 && index < GetDocumentCount()) {
            return document_ids_[index];
        }
        throw out_of_range("document index not present");
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& rawQuery, int documentId) const {
        Query query;
        if (!parseQuery(rawQuery, query)) {
            throw invalid_argument("Bad query");
        }
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(documentId)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(documentId)) {
                matched_words.clear();
                break;
            }
        }
        return { matched_words, documents_.at(documentId).status };
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
        vector<string> words;
    };

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;
    set<string> m_stopWords;
    vector<int> document_ids_;

    [[nodiscard]]
    bool parseQuery(const string& text, Query& query) const {
        vector<string> words;
        split(words, text);
        for (const string& word : words) {
            QueryWord query_word;
            if (!parseQueryWord(word, query_word)) {
                return false;
            }
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                }
                else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return true;
    }

    [[nodiscard]]
    bool parseQueryWord(string text, QueryWord& qw) const {
        if (text.empty()) {
            return false;
        }
        bool is_minus = false;
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        if (!isValidWord(text)) {
            return false;
        }
        qw = { text, is_minus, isStopWord(text) };
        return true;
    }

    bool addDocumentData(int document_id, const vector<string>& words, DocumentStatus status, const vector<int>& ratings) {
        if (documents_.count(document_id) == 0) {
            documents_.emplace(document_id, DocumentData{ computeAverageRating(ratings), status, words });
            return true;
        }
        return false;
    }

    static int computeAverageRating(const vector<int>& ratings) {
        int ratingsCount = static_cast<int>(ratings.size());
        if (ratingsCount == 0) {
            return 0;
        }
        else {
            int rating_sum = accumulate(ratings.cbegin(), ratings.cend(), 0);
            return rating_sum / static_cast<int>(ratings.size());
        }
    }

    void calculateTermFrequency(int documentId) {
        const vector<string>& words = documents_[documentId].words;
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            if (word_to_document_freqs_[word].count(documentId) == 0) {
                word_to_document_freqs_[word][documentId] = 0.0;
            }
            word_to_document_freqs_[word][documentId] += inv_word_count;
        }
    }

    bool splitIntoWordsNoStop(const string& text, vector<string>& words) const {
        vector<string> tempWords;
        split(tempWords, text);
        for (const string& word : tempWords) {
            if (!isValidWord(word)) {
                return false;
            }
            if (!isStopWord(word)) {
                words.push_back(word);
            }
        }
        return true;
    }

    static bool isValidChar(char character, bool isFirst) {
        if (character < 0) {
            return true;
        }
        if (iscntrl(character)) {
            return false;
        }
        if (isFirst && isalpha(character)) {
            return true;
        }
        if (!isFirst && isgraph(character)) {
            return true;
        }
        return false;
    }

    static bool isValidWord(const string& word) {
        if (word.empty()) {
            return false;
        }
        for (auto [it, end, i] = its_and_idx(word); it != end; ++it, ++i) {
            if (!isValidChar(*it, i == 0)) {
                return false;
            }
        }
        return true;
    }

    bool isStopWord(const string& word) const {
        return m_stopWords.count(word) > 0;
    }

    template<typename Container>
    static void split(Container& out, const string& s, const string& delims = " \r\n\t\v"s) {
        auto begIdx = s.find_first_not_of(delims);
        while (begIdx != string::npos) {
            auto endIdx = s.find_first_of(delims, begIdx);
            if (endIdx == string::npos) {
                endIdx = s.length();
            }
            insert_in_container(out, s.substr(begIdx, endIdx - begIdx));
            begIdx = s.find_first_not_of(delims, endIdx);
        }
    }

    template <typename DocumentPredicate>
    vector<Document> findAllDocuments(const Query& query, DocumentPredicate document_predicate) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = computeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                const auto& document_data = documents_.at(document_id);
                if (document_predicate(document_id, document_data.status, document_data.rating)) {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto [document_id, relevance] : document_to_relevance) {
            matched_documents.push_back({
                document_id,
                relevance,
                documents_.at(document_id).rating
            });
        }
        return matched_documents;
    }

    double computeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }
};

template<typename Key, typename Value>
ostream& operator<<(ostream& out, const pair<Key, Value>& container) {
    out << container.first;
    out << ": ";
    out << container.second;
    return out;
}

template<typename T>
void Print(ostream& out, T container) {
    bool first = true;
    for (const auto& element : container) {
        if (first) {
            out << element;
            first = false;
        }
        else {
            out << ", "s << element;
        }
    }
}

template<typename Element>
ostream& operator<<(ostream& out, const vector<Element>& container) {
    out << "[";
    Print(out, container);
    out << "]";
    return out;
}

template<typename Element>
ostream& operator<<(ostream& out, const set<Element>& container) {
    out << "{";
    Print(out, container);
    out << "}";
    return out;
}

template<typename Key, typename Value>
ostream& operator<<(ostream& out, const map<Key, Value>& container) {
    out << "{";
    Print(out, container);
    out << "}";
    return out;
}

template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u, const string& t_str, const string& u_str, const string& file,
                     const string& func, unsigned line, const string& hint) {
    if (t != u) {
        cerr << boolalpha;
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cerr << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

template <typename K, typename V>
void AssertEqualImpl(const map<K, V>& t, const map<K, V>& u, const string& t_str, const string& u_str, const string& file,
                     const string& func, unsigned line, const string& hint) {

    if (!equal(t.cbegin(), t.cend(), u.cbegin())) {
        cerr << boolalpha;
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cerr << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

template <typename T>
void AssertEqualImpl(const set<T>& t, const set<T>& u, const string& t_str, const string& u_str, const string& file,
                     const string& func, unsigned line, const string& hint) {

    if (!equal(t.cbegin(), t.cend(), u.cbegin())) {
        cerr << boolalpha;
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cerr << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

template <typename T>
void AssertEqualImpl(const vector<T>& t, const vector<T>& u, const string& t_str, const string& u_str, const string& file,
                     const string& func, unsigned line, const string& hint) {

    if (!equal(t.cbegin(), t.cend(), u.cbegin())) {
        cerr << boolalpha;
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cerr << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))

void AssertImpl(bool value, const string& expr_str, const string& file, const string& func, unsigned line,
                const string& hint) {
    if (!value) {
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT("s << expr_str << ") failed."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

#define ASSERT(expr) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_HINT(expr, hint) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))

template <typename FX>
void RunTestImpl(FX fn, const string& fnName) {
    fn();
    cerr << fnName << " OK" << endl;
}

#define RUN_TEST(func) RunTestImpl((func), #func)

// -------- Начало модульных тестов поисковой системы ----------

// Тест проверяет, что поисковая система исключает стоп-слова при добавлении документов
void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = { 1, 2, 3 };

    // Сначала убеждаемся, что поиск слова, не входящего в список стоп-слов,
    // находит нужный документ
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    // Затем убеждаемся, что поиск этого же слова, входящего в список стоп-слов,
    // возвращает пустой результат
    {
        SearchServer server;
        server.SetStopWords("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT_HINT(server.FindTopDocuments("in"s).empty(), "Stop words must be excluded from documents"s);
    }
}

// Тест проверяет, добавление документов. Добавленный документ должен находиться по поисковому запросу, который содержит слова из документа.
void TestAddDocument() {
    const int doc_id1 = 2;
    const string content1 = "cat in the city"s;
    const vector<int> ratings1 = { 3, 1, -1 };

    const int doc_id2 = 7;
    const string content2 = "porco rosso the crimson pig on a plane"s;
    const vector<int> ratings2 = { 2, 5, 6 };

    const int doc_id3 = 9;
    const string content3 = "black cat kyle"s;
    const vector<int> ratings3 = { -3, 2, 8 };


    {
        SearchServer server;
        server.SetStopWords("and in on the"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        const auto found_docs = server.FindTopDocuments("pig"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id2);
    }

    {
        SearchServer server;
        server.SetStopWords("and in on the"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        const auto found_docs = server.FindTopDocuments("cat -black"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        ASSERT_EQUAL(found_docs[0].id, doc_id1);
    }

    {
        SearchServer server;
        server.SetStopWords("and in on the"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        const auto found_docs = server.FindTopDocuments("cat -city"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        ASSERT_EQUAL(found_docs[0].id, doc_id3);
    }

    {
        SearchServer server;
        server.SetStopWords("and in on the"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        const auto found_docs = server.FindTopDocuments("starling"s);
        ASSERT(found_docs.empty());
    }
}

// Тест проверяет, поддержку стоп-слов. Стоп-слова исключаются из текста документов.
void TestExcludeStopWords() {
    const int doc_id1 = 2;
    const string content1 = "cat in the city"s;
    const int doc_id2 = 7;
    const string content2 = "porco rosso the crimson pig on a plane"s;
    const vector<int> ratings1 = { 3, 1, -1 };
    const vector<int> ratings2 = { 2, 5, 6 };

    {
        SearchServer server;
        server.SetStopWords("and in on the"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        const auto found_docs = server.FindTopDocuments("the"s);
        ASSERT(found_docs.empty());
    }
}

// Тест проверяет, поддержку минус-слов. Документы, содержащие минус-слова поискового запроса, не должны включаться в результаты поиска.
void TestMinusWords() {
    const int doc_id1 = 2;
    const string content1 = "cat in the city"s;
    const vector<int> ratings1 = { 3, 1, -1 };

    const int doc_id2 = 7;
    const string content2 = "porco rosso the crimson pig on a plane"s;
    const vector<int> ratings2 = { 2, 5, 6 };

    const int doc_id3 = 9;
    const string content3 = "big city bright lights"s;
    const vector<int> ratings3 = { 4, -2, 5 };

    {
        SearchServer server;
        server.SetStopWords("and in on the"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        const auto found_docs = server.FindTopDocuments("city -cat"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id3);
    }

    {
        SearchServer server;
        server.SetStopWords("and in on the"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        const auto found_docs = server.FindTopDocuments("pig -plane"s);
        ASSERT(found_docs.empty());
    }
}

// Тест проверяет, матчинг документов. При матчинге документа по поисковому запросу должны быть
// возвращены все слова из поискового запроса, присутствующие в документе. Если есть соответствие
// хотя бы по одному минус-слову, должен возвращаться пустой список слов.
void TestMatch() {
    const int doc_id1 = 2;
    const string content1 = "big cat in the city"s;
    const vector<int> ratings1 = { 3, 1, -1 };

    const int doc_id2 = 7;
    const string content2 = "porco rosso the crimson pig on a plane"s;
    const vector<int> ratings2 = { 2, 5, 6 };

    const int doc_id3 = 9;
    const string content3 = "big city bright lights"s;
    const vector<int> ratings3 = { 4, -2, 5 };

    {
        SearchServer server;
        server.SetStopWords("and in on the"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);

        const auto [words, status] = server.MatchDocument("big cat"s, doc_id1);
        ASSERT_EQUAL(words.size(), 2u);
        ASSERT((words[0] == "cat"s && words[1] == "big"s) || (words[1] == "cat"s && words[0] == "big"s));
    }

    {
        SearchServer server;
        server.SetStopWords("and in on the"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);

        const auto [words, status] = server.MatchDocument("city cat"s, doc_id1);
        ASSERT_EQUAL(words.size(), 2u);
        ASSERT((words[0] == "cat"s && words[1] == "city"s) || (words[0] == "city"s && words[1] == "cat"s));
    }

    {
        SearchServer server;
        server.SetStopWords("and in on the"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        const auto [words, status] = server.MatchDocument("the big -cat"s, doc_id1);
        ASSERT(words.empty());
    }
}

// Тест проверяет, сортировку найденных документов по релевантности. Возвращаемые при поиске
// документов результаты должны быть отсортированы в порядке убывания релевантности.
void TestRelevanceSort() {

    const int doc_id1 = 2;
    const string content1 = "white cat and fashion collar"s;
    const vector<int> ratings1 = { 8, -3 };

    const int doc_id2 = 7;
    const string content2 = "fluffy cat fluffy tail"s;
    const vector<int> ratings2 = { 7, 2, 7 };

    const int doc_id3 = 9;
    const string content3 = "groomed dog expressive eyes"s;
    const vector<int> ratings3 = { 5, -12, 2, 1 };

    const int doc_id4 = 10;
    const string content4 = "groomed starling evgen"s;
    const vector<int> ratings4 = { 9 };

    {
        SearchServer server;
        server.SetStopWords("and in on the with"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        server.AddDocument(doc_id4, content4, DocumentStatus::BANNED, ratings4);

        const auto found_docs = server.FindTopDocuments("fluffy groomed cat with collar"s);
        ASSERT_EQUAL(found_docs.size(), 3u);
        ASSERT_EQUAL(found_docs[0].id, doc_id2);
        ASSERT_EQUAL(found_docs[1].id, doc_id1);
        ASSERT_EQUAL(found_docs[2].id, doc_id3);

        for (size_t i = 1; i < found_docs.size(); ++i) {
            ASSERT(found_docs[i - 1].relevance >= found_docs[i].relevance);
        }
    }
}

// Тест проверяет, вычисление рейтинга документов. Рейтинг добавленного
// документа равен среднему арифметическому оценок документа.
void TestRating() {

    const int doc_id1 = 2;
    const string content1 = "white cat and fashion collar"s;
    const vector<int> ratings1 = { 8, -3 };

    const int doc_id2 = 7;
    const string content2 = "fluffy cat fluffy tail"s;
    const vector<int> ratings2 = { 257, 26, 769 };

    const int doc_id3 = 9;
    const string content3 = "groomed dog expressive eyes"s;
    const vector<int> ratings3 = { 75698, -12359, 28964, 13654 };

    const int doc_id4 = 10;
    const string content4 = "groomed starling evgen"s;
    const vector<int> ratings4 = { 9 };

    const int doc_id5 = 11;
    const string content5 = "red spider peter with black abdomen"s;
    const vector<int> ratings5;

    {
        SearchServer server;
        server.SetStopWords("and in on the with"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        server.AddDocument(doc_id4, content4, DocumentStatus::BANNED, ratings4);
        server.AddDocument(doc_id5, content5, DocumentStatus::ACTUAL, ratings5);

        const auto found_docs = server.FindTopDocuments("white cat -fluffy"s);
        int rating_sum = accumulate(ratings1.cbegin(), ratings1.cend(), 0);
        int r = rating_sum / static_cast<int>(ratings1.size());
        ASSERT_EQUAL(found_docs.size(), 1u);
        ASSERT_EQUAL(found_docs[0].rating, r);
    }

    {
        SearchServer server;
        server.SetStopWords("and in on the with"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        server.AddDocument(doc_id4, content4, DocumentStatus::BANNED, ratings4);
        server.AddDocument(doc_id5, content5, DocumentStatus::ACTUAL, ratings5);

        const auto found_docs = server.FindTopDocuments("spider"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        ASSERT_EQUAL(found_docs[0].rating, 0);
    }
}

// Тест проверяет, фильтрацию результатов поиска с использованием предиката, задаваемого пользователем.
void TestLambdaFiltering() {

    const int doc_id1 = 2;
    const string content1 = "white cat and fashion collar"s;
    const vector<int> ratings1 = { 8, -3 };

    const int doc_id2 = 7;
    const string content2 = "fluffy cat fluffy tail"s;
    const vector<int> ratings2 = { 7, 2, 7 };

    const int doc_id3 = 9;
    const string content3 = "groomed dog expressive eyes"s;
    const vector<int> ratings3 = { 5, -12, 2, 1 };

    const int doc_id4 = 10;
    const string content4 = "groomed starling evgen"s;
    const vector<int> ratings4 = { 9 };

    {
        SearchServer server;
        server.SetStopWords("and in on the with"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        server.AddDocument(doc_id4, content4, DocumentStatus::BANNED, ratings4);

        const auto found_docs = server.FindTopDocuments("fluffy groomed cat with collar"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::ACTUAL && rating < 0 && document_id == 9; });
        ASSERT_EQUAL(found_docs.size(), 1u);
        ASSERT_EQUAL(found_docs[0].id, doc_id3);
    }
}

// Тест проверяет, поиск документов, имеющих заданный статус.
void TestFilteringStatus() {

    const int doc_id1 = 2;
    const string content1 = "white cat and fashion collar"s;
    const vector<int> ratings1 = { 8, -3 };

    const int doc_id2 = 7;
    const string content2 = "fluffy cat fluffy tail"s;
    const vector<int> ratings2 = { 7, 2, 7 };

    const int doc_id3 = 9;
    const string content3 = "groomed dog expressive eyes"s;
    const vector<int> ratings3 = { 5, -12, 2, 1 };

    const int doc_id4 = 10;
    const string content4 = "groomed starling evgen"s;
    const vector<int> ratings4 = { 9 };

    const int doc_id6 = 15;
    const string content6 = "black bat wayne with black ears"s;
    const vector<int> ratings6 = { -3, 8, 4 };

    const int doc_id7 = 16;
    const string content7 = "red spider peter with black abdomen"s;
    const vector<int> ratings7 = { 2, 1, 6 };

    {
        SearchServer server;
        server.SetStopWords("and in on the with"s);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        server.AddDocument(doc_id4, content4, DocumentStatus::BANNED, ratings4);
        server.AddDocument(doc_id6, content6, DocumentStatus::REMOVED, ratings6);
        server.AddDocument(doc_id7, content7, DocumentStatus::IRRELEVANT, ratings7);

        const auto found_docs1 = server.FindTopDocuments("evgen"s, DocumentStatus::BANNED);
        ASSERT_EQUAL(found_docs1.size(), 1u);
        ASSERT_EQUAL(found_docs1[0].id, doc_id4);

        const auto found_docs2 = server.FindTopDocuments("wayne"s, DocumentStatus::REMOVED);
        ASSERT_EQUAL(found_docs2.size(), 1u);
        ASSERT_EQUAL(found_docs2[0].id, doc_id6);

        const auto found_docs3 = server.FindTopDocuments("peter"s, DocumentStatus::IRRELEVANT);
        ASSERT_EQUAL(found_docs3.size(), 1u);
        ASSERT_EQUAL(found_docs3[0].id, doc_id7);
    }
}

// Тест проверяет, корректное вычисление релевантности найденных документов.
void TestRelevance() {

    const int doc_id1 = 2;
    const string content1 = "white cat and fashion collar"s;
    const vector<int> ratings1 = { 8, -3 };

    const int doc_id2 = 7;
    const string content2 = "fluffy cat fluffy tail"s;
    const vector<int> ratings2 = { 7, 2, 7 };

    const int doc_id3 = 9;
    const string content3 = "groomed dog expressive eyes"s;
    const vector<int> ratings3 = { 5, -12, 2, 1 };

    const int doc_id4 = 10;
    const string content4 = "groomed starling evgen"s;
    const vector<int> ratings4 = { 9 };

    const int doc_id5 = 13;
    const string content5 = "black penguin oswald with black collar"s;
    const vector<int> ratings5 = { 7, 3, 8 };

    const int doc_id6 = 15;
    const string content6 = "black bat wayne with black ears"s;
    const vector<int> ratings6 = { -3, 8, 4 };

    const int doc_id7 = 16;
    const string content7 = "red spider peter with black abdomen"s;
    const vector<int> ratings7 = { 2, 1, 6 };

    {
        SearchServer server;
        string stopWords = "and in on the with"s;
        server.SetStopWords(stopWords);
        server.AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        server.AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        server.AddDocument(doc_id4, content4, DocumentStatus::BANNED, ratings4);
        server.AddDocument(doc_id5, content5, DocumentStatus::REMOVED, ratings5);
        server.AddDocument(doc_id6, content6, DocumentStatus::REMOVED, ratings6);
        server.AddDocument(doc_id7, content7, DocumentStatus::IRRELEVANT, ratings7);

        auto double_equals = [](double a, double b, double epsilon = 1e-6) {
            return std::abs(a - b) < epsilon;
        };

        //2     white cat and fashion collar
        //7     fluffy cat fluffy tail
        //9     groomed dog expressive eyes
        //10    groomed starling evgen
        //15    black bat wayne with black ears
        //16    red spider peter with black abdomen

        //N     Name        id/TF
        //==================================
        //1     abdomen	    {16|0,20}
        //2     bat	        {15|0,20}
        //3     black 	    {15|0,40}   {16/0,20}
        //4     cat	        { 2|0,25} 	{ 7/0,25}
        //5     collar	    { 2|0,25}
        //6     dog	        { 9|0,25}
        //7     ears	    {15|0,20}
        //8     evgen	    {10|0,33}
        //9     expressive  { 9|0,25}
        //10    eyes	    { 9|0,25}
        //11    fashion	    { 2|0,25}
        //12    fluffy	    { 7|0,50}
        //13    groomed	    { 9|0,25} 	{10/0,33}
        //14    peter	    {16|0,20}
        //15    red	        {16|0,20}
        //16    spider	    {16|0,20}
        //17    starling  	{10|0,33}
        //18    tail	    { 7|0,25}
        //19    wayne	    {15|0,20}
        //20    white	    { 2|0,25}

        //fluffy groomed cat with collar

        double totalDocCount = 7.0;

        //12 fluffy 7
        double fluffyDocCount = 1.0f;
        double fluffyIDF = log(totalDocCount * 1.0 / fluffyDocCount);

        //13 groomed 9 10
        double groomedDocCount = 2.0f;
        double groomedIDF = log(totalDocCount * 1.0 / groomedDocCount);

        //4 cat 2 7
        double catDocCount = 2.0f;
        double catIDF = log(totalDocCount * 1.0 / catDocCount);

        //5 collar 2
        double collarDocCount = 1.0f;
        double collarIDF = log(totalDocCount * 1.0 / collarDocCount);

        double relevance = groomedIDF * 0.25;
        const auto found_docs = server.FindTopDocuments("fluffy groomed cat with collar"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::ACTUAL && rating < 0 && document_id == 9; });
        ASSERT_EQUAL(found_docs.size(), 1u);
        ASSERT(double_equals(found_docs[0].relevance, relevance));


        set<string> stop_words_;
        map<string, map<int, double>> word_to_document_freqs_;
        map<int, pair<int, DocumentStatus>> documents_;

        auto SplitIntoWords = [](const string& text) {
            vector<string> words;
            string word;
            for (const char c : text) {
                if (c == ' ') {
                    words.push_back(word);
                    word = "";
                }
                else {
                    word += c;
                }
            }
            words.push_back(word);

            return words;
        };

        for (const string& word : SplitIntoWords(stopWords)) {
            stop_words_.insert(word);
        }

        auto IsStopWord = [&](const string& word) {
            return stop_words_.count(word) > 0;
        };

        auto SplitIntoWordsNoStop = [&](const string& text) {
            vector<string> words;
            for (const string& word : SplitIntoWords(text)) {
                if (!IsStopWord(word)) {
                    words.push_back(word);
                }
            }
            return words;
        };

        auto ComputeAverageRating = [](const vector<int>& ratings) {
            int rating_sum = accumulate(ratings.cbegin(), ratings.cend(), 0);
            return rating_sum / static_cast<int>(ratings.size());
        };

        auto AddDocument = [&](int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
            const vector<string> words = SplitIntoWordsNoStop(document);
            const double inv_word_count = 1.0 / words.size();
            for (const string& word : words) {
                if (word_to_document_freqs_[word].count(document_id) == 0) {
                    word_to_document_freqs_[word][document_id] = 0.0;
                }
                word_to_document_freqs_[word][document_id] += inv_word_count;
            }
            documents_.emplace(document_id, make_pair(ComputeAverageRating(ratings), status));
        };

        AddDocument(doc_id1, content1, DocumentStatus::ACTUAL, ratings1);
        AddDocument(doc_id2, content2, DocumentStatus::ACTUAL, ratings2);
        AddDocument(doc_id3, content3, DocumentStatus::ACTUAL, ratings3);
        AddDocument(doc_id4, content4, DocumentStatus::BANNED, ratings4);
        AddDocument(doc_id5, content5, DocumentStatus::REMOVED, ratings5);
        AddDocument(doc_id6, content6, DocumentStatus::REMOVED, ratings6);
        AddDocument(doc_id7, content7, DocumentStatus::IRRELEVANT, ratings7);

        auto ParseQueryWord = [&](string text) {
            bool is_minus = false;
            // Word shouldn't be empty
            if (text[0] == '-') {
                is_minus = true;
                text = text.substr(1);
            }
            return make_tuple(text, is_minus, IsStopWord(text));
        };

        auto ParseQuery = [&](const string& text) {
            pair<set<string>, set<string>> query;
            for (const string& word : SplitIntoWords(text)) {
                string data;
                bool is_minus;
                bool is_stop;
                tie(data, is_minus, is_stop) = ParseQueryWord(word);
                if (!is_stop) {
                    if (is_minus) {
                        query.second.insert(data);
                    }
                    else {
                        query.first.insert(data);
                    }
                }
            }
            return query;
        };

        auto GetDocumentCount = [&]() {
            return documents_.size();
        };

        auto ComputeWordInverseDocumentFreq = [&](const string& word) {
            return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
        };

        auto FindAllDocuments = [&](const pair<set<string>, set<string>>& query, function<bool(int document_id, DocumentStatus status, int rating)> fn) {
            map<int, double> document_to_relevance;
            for (const string& word : query.first) {
                if (word_to_document_freqs_.count(word) == 0) {
                    continue;
                }
                const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                    if (fn(document_id, documents_.at(document_id).second, documents_.at(document_id).first)) {
                        document_to_relevance[document_id] += term_freq * inverse_document_freq;
                    }
                }
            }

            for (const string& word : query.second) {
                if (word_to_document_freqs_.count(word) == 0) {
                    continue;
                }
                for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                    document_to_relevance.erase(document_id);
                }
            }

            vector<Document> matched_documents;
            for (const auto [document_id, relevance] : document_to_relevance) {
                matched_documents.push_back({
                    document_id,
                    relevance,
                    documents_.at(document_id).first
                });
            }
            return matched_documents;
        };

        auto FindTopDocuments = [&](const string& raw_query, function<bool(int document_id, DocumentStatus status, int rating)> fn) {
            const pair<set<string>, set<string>> query = ParseQuery(raw_query);
            vector<Document> matched_documents = FindAllDocuments(query, fn);
            sort(
                matched_documents.begin(),
                matched_documents.end(),
                [](const Document& lhs, const Document& rhs) {
                return lhs.relevance > rhs.relevance || (abs(lhs.relevance - rhs.relevance) < 1e-6 && lhs.rating > rhs.rating);
            }
            );
            if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
                matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
            }
            return matched_documents;
        };

        const auto found_docs1 = server.FindTopDocuments("fluffy groomed cat with collar"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::ACTUAL && rating < 0 && document_id == 9; });
        const auto found_docs2 = FindTopDocuments("fluffy groomed cat with collar"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::ACTUAL && rating < 0 && document_id == 9; });

        ASSERT_EQUAL(found_docs1.size(), found_docs2.size());
        ASSERT(double_equals(found_docs1[0].relevance, found_docs2[0].relevance));

        const auto found_docs3 = server.FindTopDocuments("fluffy groomed cat with collar"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::ACTUAL; });
        const auto found_docs4 = FindTopDocuments("fluffy groomed cat with collar"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::ACTUAL; });

        ASSERT_EQUAL(found_docs3.size(), found_docs3.size());
        ASSERT(double_equals(found_docs3[0].relevance, found_docs4[0].relevance));
        ASSERT(double_equals(found_docs3[1].relevance, found_docs4[1].relevance));
        ASSERT(double_equals(found_docs3[2].relevance, found_docs4[2].relevance));

        const auto found_docs5 = server.FindTopDocuments("fluffy groomed cat with collar"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::BANNED; });
        const auto found_docs6 = FindTopDocuments("fluffy groomed cat with collar"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::BANNED; });

        ASSERT_EQUAL(found_docs5.size(), found_docs6.size());
        ASSERT(double_equals(found_docs5[0].relevance, found_docs6[0].relevance));

        const auto found_docs7 = server.FindTopDocuments("penguin"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::REMOVED; });
        const auto found_docs8 = FindTopDocuments("penguin"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::REMOVED; });

        ASSERT_EQUAL(found_docs7.size(), found_docs8.size());
        ASSERT(found_docs7[0].id == found_docs8[0].id);
        ASSERT(double_equals(found_docs7[0].relevance, found_docs8[0].relevance));

        const auto found_docs9 = server.FindTopDocuments("spider"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::IRRELEVANT; });
        const auto found_docs10 = FindTopDocuments("spider"s, [](int document_id, DocumentStatus status, int rating) { return status == DocumentStatus::IRRELEVANT; });

        ASSERT_EQUAL(found_docs9.size(), found_docs10.size());
        ASSERT(found_docs9[0].id == found_docs10[0].id);
        ASSERT(double_equals(found_docs9[0].relevance, found_docs10[0].relevance));
    }
}

// Функция TestSearchServer является точкой входа для запуска тестов
void TestSearchServer() {
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    RUN_TEST(TestAddDocument);
    RUN_TEST(TestExcludeStopWords);
    RUN_TEST(TestMinusWords);
    RUN_TEST(TestMatch);
    RUN_TEST(TestRelevanceSort);
    RUN_TEST(TestRating);
    RUN_TEST(TestLambdaFiltering);
    RUN_TEST(TestFilteringStatus);
    RUN_TEST(TestRelevance);
}

// ------------ Пример использования ----------------

void PrintDocument(const Document& document) {
    cout << "{ "s
        << "document_id = "s << document.id << ", "s
        << "relevance = "s << document.relevance << ", "s
        << "rating = "s << document.rating << " }"s << endl;
}

void PrintMatchDocumentResult(int document_id, const vector<string>& words, DocumentStatus status) {
    cout << "{ "s
        << "document_id = "s << document_id << ", "s
        << "status = "s << static_cast<int>(status) << ", "s
        << "words ="s;
    for (const string& word : words) {
        cout << ' ' << word;
    }
    cout << "}"s << endl;
}

void AddDocument(SearchServer& search_server, int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
    try {
        search_server.AddDocument(document_id, document, status, ratings);
    }
    catch (const exception& e) {
        cout << "Ошибка добавления документа "s << document_id << ": "s << e.what() << endl;
    }
}

void FindTopDocuments(const SearchServer& search_server, const string& raw_query) {
    cout << "Результаты поиска по запросу: "s << raw_query << endl;
    try {
        for (const Document& document : search_server.FindTopDocuments(raw_query)) {
            PrintDocument(document);
        }
    }
    catch (const exception& e) {
        cout << "Ошибка поиска: "s << e.what() << endl;
    }
}

void MatchDocuments(const SearchServer& search_server, const string& query) {
    try {
        cout << "Матчинг документов по запросу: "s << query << endl;
        const int document_count = search_server.GetDocumentCount();
        for (int index = 0; index < document_count; ++index) {
            const int document_id = search_server.GetDocumentId(index);
            const auto [words, status] = search_server.MatchDocument(query, document_id);
            PrintMatchDocumentResult(document_id, words, status);
        }
    }
    catch (const exception& e) {
        cout << "Ошибка матчинга документов на запрос "s << query << ": "s << e.what() << endl;
    }
}

int main() {
    TestSearchServer();

    {
        SearchServer search_server("и в на"s);

        AddDocument(search_server, 1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });
        AddDocument(search_server, 1, "пушистый пёс и модный ошейник"s, DocumentStatus::ACTUAL, { 1, 2 });
        AddDocument(search_server, -1, "пушистый пёс и модный ошейник"s, DocumentStatus::ACTUAL, { 1, 2 });
        AddDocument(search_server, 3, "большой пёс скво\x12рец евгений"s, DocumentStatus::ACTUAL, { 1, 3, 2 });
        AddDocument(search_server, 4, "большой пёс скворец евгений"s, DocumentStatus::ACTUAL, { 1, 1, 1 });

        FindTopDocuments(search_server, "пушистый -пёс"s);
        FindTopDocuments(search_server, "пушистый --кот"s);
        FindTopDocuments(search_server, "пушистый -"s);

        MatchDocuments(search_server, "пушистый пёс"s);
        MatchDocuments(search_server, "модный -кот"s);
        MatchDocuments(search_server, "модный --пёс"s);
        MatchDocuments(search_server, "пушистый - хвост"s);
    }

    {
        SearchServer search_server;
        search_server.SetStopWords("и в на"s);

        search_server.AddDocument(0, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, { 8, -3 });
        search_server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });
        search_server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, { 5, -12, 2, 1 });
        search_server.AddDocument(3, "ухоженный скворец евгений"s, DocumentStatus::BANNED, { 9 });

        cout << "ACTUAL by default:"s << endl;
        for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный кот"s)) {
            PrintDocument(document);
        }

        cout << "BANNED:"s << endl;
        for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный кот"s, DocumentStatus::BANNED)) {
            PrintDocument(document);
        }

        cout << "Even ids:"s << endl;
        for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный кот"s, [](int document_id, DocumentStatus status, int rating) { return document_id % 2 == 0; })) {
            PrintDocument(document);
        }
    }

    return 0;
}