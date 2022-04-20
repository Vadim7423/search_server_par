#include <numeric>
#include <execution>
#include <algorithm>
#include <list>

#include "process_queries.h"

std::vector<std::vector<Document>> ProcessQueries(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) 
{
    std::vector<std::vector<Document>> documents_lists(queries.size());

    std::transform(
        std::execution::par,
        queries.begin(), queries.end(),
        documents_lists.begin(),
        [&search_server](const std::string& query) { return search_server.FindTopDocuments(query); }
    );

    return documents_lists;
}

std::vector<Document> ProcessQueriesJoined(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) 
{
    std::vector<std::vector<Document>> q_results = ProcessQueries(search_server, queries);
    std::vector<Document> result;

    for(const auto& item : q_results){
        for(const auto& i : item){
            result.push_back(i);
        }
    }


    return result;
}
