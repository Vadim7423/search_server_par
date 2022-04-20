#pragma once

#include <deque>

#include "test_example_functions.h"

class RequestQueue {
public:
    explicit RequestQueue(const SearchServer& search_server)
        : search_server(search_server)
    {

    }

    // сделаем "обёртки" для всех методов поиска, чтобы сохранять результаты для нашей статистики
    template <typename DocumentPredicate>
    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentPredicate document_predicate);

    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentStatus status);

    std::vector<Document> AddFindRequest(const std::string& raw_query);

    int GetNoResultRequests() const;

private:
    struct QueryResult {
        int empty_reqs;
        int number;
    };

    std::deque<QueryResult> requests_;
    const static int min_in_day_ = 1440;
    const SearchServer& search_server;

    void SetQueryRquest(const std::vector<Document>& req_item);

};

template <typename DocumentPredicate>
std::vector<Document> RequestQueue::AddFindRequest(const std::string& raw_query, DocumentPredicate document_predicate) {
    auto result = search_server.FindTopDocuments(raw_query, document_predicate);
    SetQueryRquest(result);
    return result;
}
