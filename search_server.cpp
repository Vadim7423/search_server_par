#include <algorithm>

#include "search_server.h"
#include "log_duration.h" // Профилировщик. Нужен был для отладки.

using namespace std::string_literals;

SearchServer::SearchServer(const std::string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))  // Invoke delegating constructor
{}                                                     // from string container

SearchServer::SearchServer(std::string_view stop_words_text)
    : SearchServer(SplitIntoWordsView(stop_words_text))  // Invoke delegating constructor
{}

void SearchServer::AddDocument(int document_id, const std::string_view document, DocumentStatus status, const std::vector<int>& ratings) {
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw std::invalid_argument("Invalid document_id"s);
    }

    documents_.emplace(document_id, DocumentData{ ComputeAverageRating(ratings), status, document.data() });
    document_ids_.insert(document_id);

    const auto words = SplitIntoWordsNoStop(documents_.at(document_id).str);
    const double inv_word_count = 1.0 / words.size();
    for (std::string_view word : words) {
        document_to_word_freqs_[document_id][word] += inv_word_count;
        word_to_document_freqs_[word][document_id] += inv_word_count;
    }
}

std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(std::execution::seq, raw_query, status);
}

std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query) const {
    return FindTopDocuments(std::execution::seq, raw_query);
}

size_t SearchServer::GetDocumentCount() const {
    return documents_.size();
}

std::map<int, std::map<std::string_view, double>> SearchServer::GetDocumentWordsFreqs() {
    return document_to_word_freqs_;
}

template <typename ExecutionPolicy>
std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(ExecutionPolicy&& policy, std::string_view raw_query, int document_id) const
{
    if (!document_ids_.count(document_id)) {
        throw std::out_of_range("incorrect document id"s);
    }

    const auto query = ParseQuery(policy, raw_query);

    std::vector<std::string_view> matched_words(query.plus_words.size());
    auto status = documents_.at(document_id).status;

    const auto pred = [this, document_id](std::string_view word) {
        const auto it = word_to_document_freqs_.find(word);
        return it != word_to_document_freqs_.end() && it->second.count(document_id);
    };

    if (std::any_of(policy, query.minus_words.begin(), query.minus_words.end(), pred)) {
        return { std::vector<std::string_view>{}, status };
    }

    auto it = std::copy_if(policy, query.plus_words.begin(), query.plus_words.end(),
        matched_words.begin(),
        pred
    );

    if constexpr (!std::is_same_v<std::decay_t<ExecutionPolicy>, std::execution::parallel_policy>) {
        matched_words.erase(it, matched_words.end());
        return { matched_words, status };
    }

    std::sort(policy, matched_words.begin(), it);
    auto last = std::unique(policy, matched_words.begin(), it);
    matched_words.erase(last, matched_words.end());

    return { matched_words, status };
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(std::string_view raw_query, int document_id) const {
    return MatchDocument(std::execution::seq, raw_query, document_id);
}

bool SearchServer::IsStopWord(std::string_view word) const {
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(std::string_view word) {
    // A valid word must not contain special characters
    return std::none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
        });
}

std::vector<std::string_view> SearchServer::SplitIntoWordsNoStop(std::string_view text) const {
    std::vector<std::string_view> words;
    for (std::string_view word : SplitIntoWordsView(text)) {
        if (!IsValidWord(word)) {
            throw std::invalid_argument("Word "s + word.data() + " is invalid"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = 0;
    for (const int rating : ratings) {
        rating_sum += rating;
    }
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(std::string_view text) const {
    bool is_minus = false;
    if (text[0] == '-') {
        is_minus = true;
        text = text.substr(1);
    }
    if (text.empty() || text[0] == '-' || !IsValidWord(text)) {
        throw std::invalid_argument("Query word "s + text.data() + " is invalid");
    }

    return { text, is_minus, IsStopWord(text) };
}


SearchServer::Query SearchServer::PushPlusMinusWords(const std::vector<std::string_view>& data) const {
    Query result;

    for (std::string_view word : data) {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            //auto it = result.q_words_.insert(query_word.data.data());
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            }
            else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }

    return result;
}

SearchServer::Query SearchServer::ParseQuery(std::execution::sequenced_policy seq, std::string_view text) const {
    std::vector<std::string_view> words = SplitIntoWordsView(text);

    std::sort(seq, words.begin(), words.end());
    auto last = std::unique(seq, words.begin(), words.end());
    words.erase(last, words.end());

    return PushPlusMinusWords(words);
}

SearchServer::Query SearchServer::ParseQuery(std::execution::parallel_policy par, std::string_view text) const {
    std::vector<std::string_view> words = SplitIntoWordsView(text);
    return PushPlusMinusWords(words);
}

SearchServer::Query SearchServer::ParseQuery(std::string_view text) const {
    return ParseQuery(std::execution::seq, text);
}

double SearchServer::ComputeWordInverseDocumentFreq(std::string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}

const std::map<std::string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {

    static std::map<std::string_view, double> dummy;

    if (!document_to_word_freqs_.count(document_id)) {
        return dummy;
    }

    return document_to_word_freqs_.at(document_id);
}

void SearchServer::RemoveDocument(int document_id) {
    //LOG_DURATION_STREAM("remove documents", std::cout);

    if (GetWordFrequencies(document_id).empty()) {
        return;
    }

    for (const auto& [key, val] : this->GetWordFrequencies(document_id)) {
        word_to_document_freqs_[key].erase(document_id);
    }

    documents_.erase(document_id);
    document_to_word_freqs_.erase(document_id);
    document_ids_.erase(document_id);
}

void SearchServer::RemoveDocument(std::execution::sequenced_policy seq, int document_id) {
    //LOG_DURATION_STREAM("remove documents", std::cout);

    const std::map<std::string_view, double>& word_freqs = GetWordFrequencies(document_id);

    if (word_freqs.empty()) {
        return;
    }

    std::for_each(seq,
        word_freqs.begin(),
        word_freqs.end(),
        [document_id, this](const auto& item) {
            word_to_document_freqs_.at(item.first).erase(document_id);
        }
    );

    documents_.erase(document_id);
    document_to_word_freqs_.erase(document_id);
    document_ids_.erase(document_id);
}

void SearchServer::RemoveDocument(std::execution::parallel_policy par, int document_id) {
    //LOG_DURATION_STREAM("remove documents", std::cout);

    const std::map<std::string_view, double>& word_freqs = GetWordFrequencies(document_id);

    if (word_freqs.empty()) {
        return;
    }

    std::for_each(par,
        word_freqs.begin(),
        word_freqs.end(),
        [document_id, this](const auto& item) {
            word_to_document_freqs_.at(item.first).erase(document_id);
        }
    );

    documents_.erase(document_id);
    document_to_word_freqs_.erase(document_id);
    document_ids_.erase(document_id);
}


