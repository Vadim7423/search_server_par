#include "request_queue.h"

std::vector<Document> RequestQueue::AddFindRequest(const std::string& raw_query, DocumentStatus status) {
    auto result = search_server.FindTopDocuments(raw_query, status);
    SetQueryRquest(result);
    return result;
}

std::vector<Document> RequestQueue::AddFindRequest(const std::string& raw_query) {
    auto result = search_server.FindTopDocuments(raw_query);
    SetQueryRquest(result);
    return result;
}

int RequestQueue::GetNoResultRequests() const {
    auto count = requests_.back();
    return count.empty_reqs;
}

void RequestQueue::SetQueryRquest(const std::vector<Document>& req_item)
{
    QueryResult item;
    item.number = requests_.empty() ? 1 : requests_.back().number+1;

    if(requests_.size() >= min_in_day_){
        requests_.pop_front();
        if(requests_.front().empty_reqs && !req_item.empty()){
            item.empty_reqs = requests_.back().empty_reqs-1;
        }else{
            item.empty_reqs = requests_.back().empty_reqs;
        }
    }else{
        if(req_item.empty()){
            item.empty_reqs = requests_.empty() ? 1 : requests_.back().empty_reqs+1;
        }else{
            item.empty_reqs = requests_.back().empty_reqs;
        }
    }

      //  cout << item.number << "-" << item.empty_reqs << endl;
    requests_.push_back(item);
}
