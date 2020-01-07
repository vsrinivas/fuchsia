// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cassert>
#include <regex>

#include <fidl/utils.h>

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
  if (has_konstant_k(str)) {
    return false;
  }
  static std::regex re{"^[a-z][a-z0-9]*(([A-Z]{1,2}[a-z0-9]+)|(_[0-9]+))*([A-Z][a-z0-9]*)?$"};
  return str.size() > 0 && std::regex_match(str, re);
}

bool is_upper_camel_case(const std::string& str) {
  static std::regex re{
      "^(([A-Z]{1,2}[a-z0-9]+)(([A-Z]{1,2}[a-z0-9]+)|(_[0-9]+))*)?([A-Z][a-z0-9]*)?$"};
  return str.size() > 0 && std::regex_match(str, re);
}

bool is_konstant_case(const std::string& astr) {
  if (!has_konstant_k(astr)) {
    return false;
  }
  std::string str = strip_konstant_k(astr);
  return is_upper_camel_case(str);
}

static void add_word(std::string word, std::vector<std::string>& words,
                     const std::set<std::string>& stop_words) {
  if (stop_words.find(word) == stop_words.end()) {
    words.push_back(word);
  }
}

std::vector<std::string> id_to_words(const std::string& astr) { return id_to_words(astr, {}); }

std::vector<std::string> id_to_words(const std::string& astr, std::set<std::string> stop_words) {
  std::string str = strip_konstant_k(astr);
  std::vector<std::string> words;
  std::string word;
  bool last_char_was_upper_or_begin = true;
  for (size_t i = 0; i < str.size(); i++) {
    char ch = str[i];
    if (ch == '_' || ch == '-' || ch == '.') {
      if (word.size() > 0) {
        add_word(word, words, stop_words);
        word.clear();
      }
      last_char_was_upper_or_begin = true;
    } else {
      bool next_char_is_lower = ((i + 1) < str.size()) && islower(str[i + 1]);
      if (isupper(ch) && (!last_char_was_upper_or_begin || next_char_is_lower)) {
        if (word.size() > 0) {
          add_word(word, words, stop_words);
          word.clear();
        }
      }
      word.push_back(static_cast<char>(tolower(ch)));
      last_char_was_upper_or_begin = isupper(ch);
    }
  }
  if (word.size() > 0) {
    add_word(word, words, stop_words);
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
  bool prev_char_was_digit = false;
  std::string newid;
  for (const auto& word : id_to_words(str)) {
    if (newid.size() == 0) {
      newid.append(word);
    } else {
      if (prev_char_was_digit && isdigit(word[0])) {
        newid.push_back('_');
      }
      newid.push_back(static_cast<char>(toupper(word[0])));
      newid.append(word.substr(1));
    }
    prev_char_was_digit = isdigit(word.back());
  }
  return newid;
}

std::string to_upper_camel_case(const std::string& astr) {
  std::string str = strip_konstant_k(astr);
  bool prev_char_was_digit = false;
  std::string newid;
  for (const auto& word : id_to_words(str)) {
    if (prev_char_was_digit && isdigit(word[0])) {
      newid.push_back('_');
    }
    newid.push_back(static_cast<char>(toupper(word[0])));
    newid.append(word.substr(1));
    prev_char_was_digit = isdigit(word.back());
  }
  return newid;
}

std::string to_konstant_case(const std::string& str) { return "k" + to_upper_camel_case(str); }

void PrintFinding(std::ostream& os, const Finding& finding) {
  os << finding.message() << " [";
  os << finding.subcategory();
  os << "]";
  if (finding.suggestion().has_value()) {
    auto& suggestion = finding.suggestion();
    os << "; " << suggestion->description();
    if (suggestion->replacement().has_value()) {
      os << "\n    Proposed replacement:  '" << *suggestion->replacement() << "'";
    }
  }
}

void WriteFindingsToErrorReporter(const Findings& findings, ErrorReporter* error_reporter) {
  for (auto& finding : findings) {
    std::stringstream ss;
    PrintFinding(ss, finding);
    error_reporter->ReportWarningWithSquiggle(finding.span(), ss.str());
  }
}

bool OnlyWhitespaceChanged(const std::string& unformatted_input,
                           const std::string& formatted_output) {
  std::string formatted = formatted_output;
  auto formatted_end = std::remove_if(formatted.begin(), formatted.end(), isspace);
  formatted.erase(formatted_end, formatted.end());

  std::string unformatted(unformatted_input);
  auto unformatted_end = std::remove_if(unformatted.begin(), unformatted.end(), isspace);
  unformatted.erase(unformatted_end, unformatted.end());

  return formatted == unformatted;
}

}  // namespace utils
}  // namespace fidl
