#pragma once

#include <algorithm>
#include <map>
#include <cmath>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <execution>
#include <type_traits>

#include "document.h"
#include "paginator.h"
#include "read_input_functions.h"
#include "string_processing.h"
#include "concurrent_map.h"
#include "log_duration.h"

#define SMALL_RANGE_FOR_COMPARE 1e-6 // для сравнения вещественных чисел.

/**
	* Ядро поискового сервера с добавленными методами, 
	* поддерживающими параллельные алгоритмы.
	**/
class SearchServer {
public:
    template <typename StringContainer>
    SearchServer(const StringContainer& stop_words);

    SearchServer(const std::string& stop_words_text);    // from string container

    SearchServer(std::string_view stop_words_text);

    void AddDocument(int document_id, std::string_view document, DocumentStatus status, const std::vector<int>& ratings);

    template <typename DocumentPredicate, typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, std::string_view raw_query, DocumentPredicate document_predicate) const;
   
    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const;
    
    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, std::string_view raw_query, DocumentStatus status) const;
    
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentStatus status) const;
    
    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, std::string_view raw_query) const;
    
    std::vector<Document> FindTopDocuments(std::string_view raw_query) const;

    size_t GetDocumentCount() const;

    std::map<int, std::map<std::string_view, double>> GetDocumentWordsFreqs();

    auto begin() const {
        return document_ids_.begin();
    }

    auto end() const {
        return document_ids_.end();
    }

    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;

    void RemoveDocument(int document_id);

    void RemoveDocument(std::execution::sequenced_policy seq, int document_id);

    void RemoveDocument(std::execution::parallel_policy par, int document_id);

    template <typename ExecutionPolicy>
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(ExecutionPolicy&& policy, std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::string_view raw_query, int document_id) const;

private:
    const int MAX_RESULT_DOCUMENT_COUNT = 5;

    struct DocumentData {
        int rating;
        DocumentStatus status;
        std::string str;
    };

    const std::set<std::string, std::less<>> stop_words_;
    std::map<std::string_view, std::map<int, double>> word_to_document_freqs_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;

    bool IsStopWord(std::string_view word) const;

    static bool IsValidWord(std::string_view word);

    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const;

    static int ComputeAverageRating(const std::vector<int>& ratings);

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(std::string_view text) const;

    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    Query ParseQuery(std::execution::sequenced_policy seq, std::string_view text) const;
    Query ParseQuery(std::execution::parallel_policy par, std::string_view text) const;
    Query ParseQuery(std::string_view text) const;
    Query PushPlusMinusWords(const std::vector<std::string_view>& data) const;

    // Existence required
    double ComputeWordInverseDocumentFreq(std::string_view word) const;

    template <typename DocumentPredicate, typename ExecutionPloicy>
    std::vector<Document> FindAllDocuments(ExecutionPloicy&& policy, const Query& query, DocumentPredicate document_predicate) const;
    template <typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const;
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
    : stop_words_(MakeUniqueNonEmptyStrings(stop_words))  // Extract non-empty stop words
{
    using namespace std::string_literals;

    if (!std::all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
        throw std::invalid_argument("Some of stop words are invalid"s);
    }
}

template <typename DocumentPredicate, typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, std::string_view raw_query, DocumentPredicate document_predicate) const {
    using namespace std::string_literals;

    const auto query = ParseQuery(raw_query);

    auto matched_documents = FindAllDocuments(policy, query, document_predicate);

    sort(policy, matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {

        if (std::abs(lhs.relevance - rhs.relevance) < SMALL_RANGE_FOR_COMPARE) {
            return lhs.rating > rhs.rating;
        }
        else {
            return lhs.relevance > rhs.relevance;
        }
        });

    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }

    return matched_documents;
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const {
    return FindTopDocuments(std::execution::seq, raw_query, document_predicate);
}

template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(policy, raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}

template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, std::string_view raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}

template <typename DocumentPredicate, typename ExecutionPolicy>
std::vector<Document> SearchServer::FindAllDocuments(ExecutionPolicy&& policy, const Query& query, DocumentPredicate document_predicate) const {
    ConcurrentMap<int, double> document_to_relevance(8);
    std::map<int, double> tmp_map;

    std::for_each(
        policy,
        query.plus_words.begin(),
        query.plus_words.end(),
        [this, &document_to_relevance, &document_predicate, &tmp_map](std::string_view word) {
            if (word_to_document_freqs_.count(word)) {
                const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                    const auto& document_data = documents_.at(document_id);
                    if (document_predicate(document_id, document_data.status, document_data.rating)) {
                        document_to_relevance[document_id].ref_to_value += term_freq * inverse_document_freq;
                    }
                }
            }
        }
    );

    std::for_each(
        policy,
        query.minus_words.begin(),
        query.minus_words.end(),
        [this, &document_predicate, &document_to_relevance](std::string_view word) {
            if (word_to_document_freqs_.count(word)) {
                for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                    document_to_relevance.erase(document_id);
                }
            }
        }
    );

    tmp_map = document_to_relevance.BuildOrdinaryMap();

    std::vector<Document> matched_documents(tmp_map.size());
    std::transform(
        policy,
        tmp_map.begin(),
        tmp_map.end(),
        matched_documents.begin(),
        [this](const auto& item) {
            return Document{ item.first, item.second, documents_.at(item.first).rating };
        }
    );

    return matched_documents;
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const {
    return FindAllDocuments(std::execution::seq, query, document_predicate);
}
