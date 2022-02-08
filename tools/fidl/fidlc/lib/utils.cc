// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/utils.h"

#include <cassert>

#include <re2/re2.h>

#include "fidl/reporter.h"

namespace fidl::utils {

const std::string kLibraryComponentPattern = "[a-z][a-z0-9]*";
const std::string kIdentifierComponentPattern = "[A-Za-z]([A-Za-z0-9_]*[A-Za-z0-9])?";

bool IsValidLibraryComponent(std::string_view component) {
  static const re2::RE2 kPattern("^" + kLibraryComponentPattern + "$");
  return re2::RE2::FullMatch(component, kPattern);
}

bool IsValidIdentifierComponent(std::string_view component) {
  static const re2::RE2 kPattern("^" + kIdentifierComponentPattern + "$");
  return re2::RE2::FullMatch(component, kPattern);
}

bool IsValidFullyQualifiedMethodIdentifier(std::string_view fq_identifier) {
  static const re2::RE2 kPattern("^" +
                                 // library identifier
                                 kLibraryComponentPattern + "(\\." + kLibraryComponentPattern +
                                 ")*" +
                                 // slash
                                 "/" +
                                 // protocol
                                 kIdentifierComponentPattern +
                                 // dot
                                 "\\." +
                                 // method
                                 kIdentifierComponentPattern + "$");
  return re2::RE2::FullMatch(fq_identifier, kPattern);
}

bool ends_with_underscore(std::string_view str) {
  assert(str.size() > 0);
  return str.back() == '_';
}

bool has_adjacent_underscores(std::string_view str) { return str.find("__") != std::string::npos; }

bool has_konstant_k(std::string_view str) {
  return str.size() >= 2 && str[0] == 'k' && isupper(str[1]);
}

std::string strip_string_literal_quotes(std::string_view str) {
  assert(str.size() >= 2 && str[0] == '"' && str[str.size() - 1] == '"' &&
         "string must start and end with '\"' style quotes");
  return std::string(str.data() + 1, str.size() - 2);
}

// NOTE: we currently explicitly only support UNIX line endings
std::string strip_doc_comment_slashes(std::string_view str) {
  // In English, this regex says: "any number of tabs/spaces, followed by three
  // slashes is group 1, the remainder of the line is group 2.  Keep only group
  // 2."
  std::string no_slashes(str);
  re2::RE2::GlobalReplace(&no_slashes, "([\\t ]*\\/\\/\\/)(.*)", "\\2");
  if (no_slashes[no_slashes.size() - 1] != '\n') {
    return no_slashes + '\n';
  }
  return no_slashes;
}

std::string strip_konstant_k(std::string_view str) {
  return std::string(has_konstant_k(str) ? str.substr(1) : str);
}

bool is_lower_no_separator_case(std::string_view str) {
  static re2::RE2 re{"^[a-z][a-z0-9]*$"};
  return str.size() > 0 && re2::RE2::FullMatch(str, re);
}

bool is_lower_snake_case(std::string_view str) {
  static re2::RE2 re{"^[a-z][a-z0-9_]*$"};
  return str.size() > 0 && re2::RE2::FullMatch(str, re);
}

bool is_upper_snake_case(std::string_view str) {
  static re2::RE2 re{"^[A-Z][A-Z0-9_]*$"};
  return str.size() > 0 && re2::RE2::FullMatch(str, re);
}

bool is_lower_camel_case(std::string_view str) {
  if (has_konstant_k(str)) {
    return false;
  }
  static re2::RE2 re{"^[a-z][a-z0-9]*(([A-Z]{1,2}[a-z0-9]+)|(_[0-9]+))*([A-Z][a-z0-9]*)?$"};
  return str.size() > 0 && re2::RE2::FullMatch(str, re);
}

bool is_upper_camel_case(std::string_view str) {
  static re2::RE2 re{
      "^(([A-Z]{1,2}[a-z0-9]+)(([A-Z]{1,2}[a-z0-9]+)|(_[0-9]+))*)?([A-Z][a-z0-9]*)?$"};
  return str.size() > 0 && re2::RE2::FullMatch(str, re);
}

bool is_konstant_case(std::string_view astr) {
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

std::vector<std::string> id_to_words(std::string_view astr) { return id_to_words(astr, {}); }

std::vector<std::string> id_to_words(std::string_view astr, std::set<std::string> stop_words) {
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

std::string to_lower_no_separator_case(std::string_view astr) {
  std::string str = strip_konstant_k(astr);
  std::string newid;
  for (const auto& word : id_to_words(str)) {
    newid.append(word);
  }
  return newid;
}

std::string to_lower_snake_case(std::string_view astr) {
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

std::string to_upper_snake_case(std::string_view astr) {
  std::string str = strip_konstant_k(astr);
  auto newid = to_lower_snake_case(str);
  std::transform(newid.begin(), newid.end(), newid.begin(), ::toupper);
  return newid;
}

std::string to_lower_camel_case(std::string_view astr) {
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

std::string to_upper_camel_case(std::string_view astr) {
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

std::string to_konstant_case(std::string_view str) { return "k" + to_upper_camel_case(str); }

std::string canonicalize(std::string_view identifier) {
  const auto size = identifier.size();
  std::string canonical;
  char prev = '_';
  for (size_t i = 0; i < size; i++) {
    const char c = identifier[i];
    if (c == '_') {
      if (prev != '_') {
        canonical.push_back('_');
      }
    } else if (((islower(prev) || isdigit(prev)) && isupper(c)) ||
               (prev != '_' && isupper(c) && i + 1 < size && islower(identifier[i + 1]))) {
      canonical.push_back('_');
      canonical.push_back(static_cast<char>(tolower(c)));
    } else {
      canonical.push_back(static_cast<char>(tolower(c)));
    }
    prev = c;
  }
  return canonical;
}

std::string StringJoin(const std::vector<std::string_view>& strings, std::string_view separator) {
  std::string result;
  bool first = true;
  for (const auto& part : strings) {
    if (!first) {
      result += separator;
    }
    first = false;
    result += part;
  }
  return result;
}

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

std::vector<std::string> FormatFindings(const Findings& findings, bool enable_color) {
  std::vector<std::string> lint;
  for (auto& finding : findings) {
    std::stringstream ss;
    PrintFinding(ss, finding);
    auto warning = Reporter::Format("warning", finding.span(), ss.str(), enable_color);
    lint.push_back(warning);
  }
  return lint;
}

bool OnlyWhitespaceChanged(std::string_view unformatted_input, std::string_view formatted_output) {
  std::string formatted(formatted_output);
  auto formatted_end = std::remove_if(formatted.begin(), formatted.end(), isspace);
  formatted.erase(formatted_end, formatted.end());

  std::string unformatted(unformatted_input);
  auto unformatted_end = std::remove_if(unformatted.begin(), unformatted.end(), isspace);
  unformatted.erase(unformatted_end, unformatted.end());

  return formatted == unformatted;
}

std::uint32_t string_literal_length(std::string_view str) {
  // -2 to account for the leading and trailing quotes
  std::uint32_t cnt = -2;
  for (auto it = str.begin(), it_end = str.end(); it < it_end; ++it) {
    ++cnt;
    if (*it == '\\') {
      ++it;
      assert(it < it_end && "compiler bug: invalid string literal");
      char next = *it;
      switch (next) {
        case 'x':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
          // Hex \xnn
          // Oct \nnn
          it += 2;
          break;
        case 'u':
          // Unicode code point: U+nnnn
          it += 4;
          break;
        case 'U':
          // Unicode code point: U+nnnnnnnn
          it += 8;
          break;
        case 'a':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
        case 'v':
        case '\\':
        case '"':
          // no additional skip required
          break;
        default:
          assert(false && "compiler bug: invalid string literal");
      }
      assert(it < it_end && "compiler bug: invalid string literal");
    }
  }
  return cnt;
}

}  // namespace fidl::utils
