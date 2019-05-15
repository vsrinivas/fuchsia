// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/utils.h>

#include <regex>

namespace fidl {
namespace utils {

bool ends_with_underscore(const std::string& str) {
    assert(str.size() > 0);
    return str.back() == '_';
}

bool has_adjacent_underscores(const std::string& str) {
    return str.find("__") != std::string::npos;
}

bool has_konstant_k(const std::string& str) {
    return str.size() >= 2 && str[0] == 'k' && isupper(str[1]);
}

std::string strip_konstant_k(const std::string& str) {
    if (has_konstant_k(str)) {
        return str.substr(1);
    } else {
        return str;
    }
}

bool is_lower_no_separator_case(const std::string& str) {
    static std::regex re{"^[a-z][a-z0-9]*$"};
    return str.size() > 0 && std::regex_match(str, re);
}

bool is_lower_snake_case(const std::string& str) {
    static std::regex re{"^[a-z][a-z0-9_]*$"};
    return str.size() > 0 && std::regex_match(str, re);
}

bool is_upper_snake_case(const std::string& str) {
    static std::regex re{"^[A-Z][A-Z0-9_]*$"};
    return str.size() > 0 && std::regex_match(str, re);
}

bool is_lower_camel_case(const std::string& str) {
    static std::regex re{"^[a-z]+([A-Z][a-z0-9]+)*$"};
    return str.size() > 0 && std::regex_match(str, re);
}

bool is_upper_camel_case(const std::string& str) {
    static std::regex re{"^([A-Z][a-z0-9]+)+$"};
    return str.size() > 0 && std::regex_match(str, re);
}

bool is_konstant_case(const std::string& astr) {
    if (!has_konstant_k(astr)) {
        return false;
    }
    std::string str = strip_konstant_k(astr);
    return is_upper_camel_case(str);
}

std::vector<std::string> id_to_words(const std::string& astr) {
    // TODO(fxb/FIDL-573): Add support for mixed case with underscores, as in
    // the example:  const int kAndroid8_0_0 = 24; // Android 8.0.0
    std::string str = strip_konstant_k(astr);
    std::vector<std::string> words;
    std::string word;
    bool last_char_was_upper_or_begin = true;
    for (size_t i = 0; i < str.size(); i++) {
        char ch = str[i];
        if (ch == '_' || ch == '-' || ch == '.') {
            if (word.size() > 0) {
                words.push_back(word);
                word.clear();
            }
            last_char_was_upper_or_begin = true;
        } else {
            bool next_char_is_upper_or_end =
                ((i + 1) >= str.size()) ||
                isupper(str[i + 1]);
            if (isupper(ch) &&
                !(last_char_was_upper_or_begin && next_char_is_upper_or_end)) {
                if (word.size() > 0) {
                    words.push_back(word);
                    word.clear();
                }
            }
            word.push_back(static_cast<char>(tolower(ch)));
            last_char_was_upper_or_begin = isupper(ch);
        }
    }
    if (word.size() > 0) {
        words.push_back(word);
    }
    return words;
}

std::string to_lower_no_separator_case(const std::string& astr) {
    std::string str = strip_konstant_k(astr);
    std::string newid;
    for (const auto& word : id_to_words(str)) {
        newid.append(word);
    }
    return newid;
}

std::string to_lower_snake_case(const std::string& astr) {
    std::string str = strip_konstant_k(astr);
    std::string newid;
    for (const auto& word : id_to_words(str)) {
        if (newid.size() > 0) {
            newid.push_back('_');
        }
        newid.append(word);
    }
    return newid;
}

std::string to_upper_snake_case(const std::string& astr) {
    std::string str = strip_konstant_k(astr);
    auto newid = to_lower_snake_case(str);
    std::transform(newid.begin(), newid.end(), newid.begin(), ::toupper);
    return newid;
}

std::string to_lower_camel_case(const std::string& astr) {
    std::string str = strip_konstant_k(astr);
    std::string newid;
    for (const auto& word : id_to_words(str)) {
        if (newid.size() == 0) {
            newid.append(word);
        } else {
            newid.push_back(static_cast<char>(toupper(word[0])));
            newid.append(word.substr(1));
        }
    }
    return newid;
}

std::string to_upper_camel_case(const std::string& astr) {
    std::string str = strip_konstant_k(astr);
    std::string newid;
    for (const auto& word : id_to_words(str)) {
        newid.push_back(static_cast<char>(toupper(word[0])));
        newid.append(word.substr(1));
    }
    return newid;
}

std::string to_konstant_case(const std::string& str) {
    return "k" + to_upper_camel_case(str);
}

void WriteFindingsToErrorReporter(const Findings& findings,
                                  ErrorReporter* error_reporter) {
    for (auto& finding : findings) {
        std::stringstream ss;
        ss << finding.message();
        if (finding.suggestion().has_value()) {
            auto& suggestion = finding.suggestion();
            ss << "; " << suggestion->description();
            if (suggestion->replacement().has_value()) {
                ss << "\nDid you mean:\n    "
                   << *suggestion->replacement();
            }
        }
        error_reporter->ReportWarningWithSquiggle(finding.source_location(),
                                                  ss.str());
    }
}

} // namespace utils
} // namespace fidl
