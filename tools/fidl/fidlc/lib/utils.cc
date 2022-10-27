// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/utils.h"

#include <zircon/assert.h>

#include <re2/re2.h>

#include "tools/fidl/fidlc/include/fidl/reporter.h"

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

bool IsValidDiscoverableName(std::string_view discoverable_name) {
  static const re2::RE2 kPattern("^" +
                                 // library identifier
                                 kLibraryComponentPattern + "(\\." + kLibraryComponentPattern +
                                 ")*" +
                                 // dot
                                 "\\." +
                                 // protocol
                                 kIdentifierComponentPattern + "$");
  return re2::RE2::FullMatch(discoverable_name, kPattern);
}

bool ends_with_underscore(std::string_view str) {
  ZX_ASSERT(!str.empty());
  return str.back() == '_';
}

bool has_adjacent_underscores(std::string_view str) { return str.find("__") != std::string::npos; }

bool has_konstant_k(std::string_view str) {
  return str.size() >= 2 && str[0] == 'k' && isupper(str[1]);
}

std::string strip_string_literal_quotes(std::string_view str) {
  ZX_ASSERT_MSG(str.size() >= 2 && str[0] == '"' && str[str.size() - 1] == '"',
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
  return !str.empty() && re2::RE2::FullMatch(str, re);
}

bool is_lower_snake_case(std::string_view str) {
  static re2::RE2 re{"^[a-z][a-z0-9_]*$"};
  return !str.empty() && re2::RE2::FullMatch(str, re);
}

bool is_upper_snake_case(std::string_view str) {
  static re2::RE2 re{"^[A-Z][A-Z0-9_]*$"};
  return !str.empty() && re2::RE2::FullMatch(str, re);
}

bool is_lower_camel_case(std::string_view str) {
  if (has_konstant_k(str)) {
    return false;
  }
  static re2::RE2 re{"^[a-z][a-z0-9]*(([A-Z]{1,2}[a-z0-9]+)|(_[0-9]+))*([A-Z][a-z0-9]*)?$"};
  return !str.empty() && re2::RE2::FullMatch(str, re);
}

bool is_upper_camel_case(std::string_view str) {
  static re2::RE2 re{
      "^(([A-Z]{1,2}[a-z0-9]+)(([A-Z]{1,2}[a-z0-9]+)|(_[0-9]+))*)?([A-Z][a-z0-9]*)?$"};
  return !str.empty() && re2::RE2::FullMatch(str, re);
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
      if (!word.empty()) {
        add_word(word, words, stop_words);
        word.clear();
      }
      last_char_was_upper_or_begin = true;
    } else {
      bool next_char_is_lower = ((i + 1) < str.size()) && islower(str[i + 1]);
      if (isupper(ch) && (!last_char_was_upper_or_begin || next_char_is_lower)) {
        if (!word.empty()) {
          add_word(word, words, stop_words);
          word.clear();
        }
      }
      word.push_back(static_cast<char>(tolower(ch)));
      last_char_was_upper_or_begin = isupper(ch);
    }
  }
  if (!word.empty()) {
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
    if (!newid.empty()) {
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
    if (newid.empty()) {
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

uint32_t decode_unicode_hex(std::string_view str) {
  char* endptr;
  unsigned long codepoint = strtoul(str.begin(), &endptr, 16);
  ZX_ASSERT(codepoint != ULONG_MAX);
  ZX_ASSERT(endptr == str.end());
  return codepoint;
}

static size_t utf8_size_for_codepoint(uint32_t codepoint) {
  if (codepoint <= 0x7f) {
    return 1;
  }
  if (codepoint <= 0x7ff) {
    return 2;
  }
  if (codepoint <= 0x10000) {
    return 3;
  }
  ZX_ASSERT(codepoint <= 0x10ffff);
  return 4;
}

std::uint32_t string_literal_length(std::string_view str) {
  std::uint32_t count = 0;
  auto it = str.begin();
  ZX_ASSERT(*it == '"');
  ++it;
  const auto closing_quote = str.end() - 1;
  for (; it < closing_quote; ++it) {
    ++count;
    if (*it == '\\') {
      ++it;
      ZX_ASSERT(it < closing_quote);
      switch (*it) {
        case '\\':
        case '"':
        case 'n':
        case 'r':
        case 't':
          break;
        case 'u': {
          ++it;
          ZX_ASSERT(*it == '{');
          ++it;
          auto codepoint_begin = it;
          while (*it != '}') {
            ++it;
          }
          auto codepoint =
              decode_unicode_hex(std::string_view(codepoint_begin, it - codepoint_begin));
          count += utf8_size_for_codepoint(codepoint) - 1;
          break;
        }
        default:
          ZX_PANIC("invalid string literal");
      }
      ZX_ASSERT(it < closing_quote);
    }
  }
  ZX_ASSERT(*it == '"');
  return count;
}

}  // namespace fidl::utils
