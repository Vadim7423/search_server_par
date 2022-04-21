#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <execution>
#include <numeric>

/**
	* Парсинг поисковых запросов.
	**/
std::vector<std::string> SplitIntoWords(std::string_view text);
std::vector<std::string_view> SplitIntoWordsView(std::string_view str);

template <typename StringContainer>
std::set<std::string, std::less<>> MakeUniqueNonEmptyStrings(const StringContainer& strings) {
    std::set<std::string, std::less<>> non_empty_strings;
    std::for_each(
        std::execution::par,
        strings.begin(),
        strings.end(),
        [&non_empty_strings](std::string_view str) {
            if (!str.empty()) {
                non_empty_strings.insert(str.data());
            }
        }
    );

    return non_empty_strings;
}
