#include "string_processing.h"
#include <execution>
#include <numeric>
#include <iostream>

using namespace std;

std::vector<std::string> SplitIntoWords(std::string_view text) {
    std::vector<std::string> words;
    std::string word;

    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(std::move(word));
               word.clear();
            }
        } else {
            word += c;
        }
    }

    if (!word.empty()) {
        words.push_back(std::move(word));
    }

    return words;
}

vector<string_view> SplitIntoWordsView(string_view str) {
    vector<string_view> result;
    const int64_t pos_end = str.npos;
    while (true) {
        int64_t space = str.find(' ', 0);
      // result.push_back(space == pos_end ? str.substr(0) : str.substr(0, space));
        if(space == pos_end){
            result.push_back(str.substr(0));
            break;
        }else{
            string_view tmp = str.substr(0, space);
            if(!tmp.empty()){
                result.push_back(tmp);
            }
            str.remove_prefix(space+1);
        }

    }

    return result;
}

