// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/dictionary.h"

#include <lib/syslog/cpp/macros.h>

#include <iomanip>
#include <sstream>

#include <re2/re2.h>

namespace fuzzing {

// Helper for printing one byte as hexadecimal.
static std::ostream& hex_byte(std::ostream& stream) {
  return stream << std::uppercase << std::setfill('0') << std::setw(2) << std::hex;
}

Dictionary& Dictionary::operator=(Dictionary&& other) noexcept {
  options_ = other.options_;
  other.options_ = nullptr;

  words_by_level_ = std::move(other.words_by_level_);

  max_level_ = other.max_level_;
  other.max_level_ = 0;

  return *this;
}

void Dictionary::AddDefaults(Options* options) {
  if (!options->has_dictionary_level()) {
    options->set_dictionary_level(kDefaultDictionaryLevel);
  }
}

void Dictionary::Configure(const std::shared_ptr<Options>& options) { options_ = options; }

void Dictionary::Add(const void* data, size_t size, uint16_t level) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(data);
  Add(Word(bytes, bytes + size), level);
}

void Dictionary::Add(Word&& word, uint16_t level) {
  max_level_ = std::max(max_level_, level);
  words_by_level_[level].push_back(std::move(word));
}

bool Dictionary::Parse(const Input& input) {
  // TODO(fxbug.dev/89119): Support parsing utf8.
  re2::RE2 blank("^\\s*(?:#.*)?$");
  re2::RE2 value("^\\s*(?:\\w+(?:@(\\d+))?\\s*=)?\\s*\"(.*)$");
  const auto* c_str = reinterpret_cast<const char*>(input.data());
  std::istringstream iss(std::string(c_str, input.size()));
  std::string line;
  size_t line_no = 0;
  std::string remaining;
  while (std::getline(iss, line)) {
    line_no++;
    uint16_t level;
    Word word;
    // Skip blank lines and comment.
    if (re2::RE2::FullMatch(line, blank)) {
      continue;
    }
    // Use a default level of 0 if omitted.
    std::string level_str;
    std::string word_str;
    if (re2::RE2::FullMatch(line, value, &level_str, &word_str)) {
      if (!ParseLevel(level_str, &level)) {
        FX_LOGS(WARNING) << "failed to parse level: '" << level_str << "' (line " << line_no << ")";
        return false;
      }
      if (!ParseWord(word_str, &word, &remaining)) {
        FX_LOGS(WARNING) << "failed to parse word: '" << word_str << "' (line " << line_no << ")";
        return false;
      }
      if (!re2::RE2::FullMatch(remaining, blank)) {
        FX_LOGS(WARNING) << "failed to parse line: '" << line << "' (line " << line_no << ")";
        return false;
      }
    } else {
      FX_LOGS(WARNING) << "failed to parse line: '" << line << "' (line " << line_no << ")";
      return false;
    }
    Add(std::move(word), level);
  }
  return true;
}

bool Dictionary::ParseLevel(const std::string& str, uint16_t* out_level) {
  if (str.empty()) {
    *out_level = 0;
    return true;
  }
  return ParseNumber(str, 10, out_level);
}

bool Dictionary::ParseWord(const std::string& str, Word* out_word, std::string* out_remaining) {
  out_word->clear();
  bool escaped = false;
  uint8_t hex_byte = 0;
  for (size_t i = 0; i < str.size(); ++i) {
    char c = str[i];
    if (escaped) {
      switch (c) {
        case '"':
        case '\\':
          out_word->push_back(static_cast<uint8_t>(c));
          break;
        case 'x':
          if (i + 2 >= str.size() || !ParseNumber(str.substr(i, 2), 16, &hex_byte)) {
            return false;
          }
          out_word->push_back(hex_byte);
          i += 2;
          break;
        default:
          return false;
      }
      escaped = false;
    } else if (c == '"') {
      *out_remaining = str.substr(i + 1);
      return !out_word->empty();
    } else if (c == '\\') {
      escaped = true;
    } else if (isprint(c) || isspace(c)) {
      out_word->push_back(static_cast<uint8_t>(c));
    } else {
      FX_LOGS(WARNING) << "invalid byte: 0x" << hex_byte << c;
      return false;
    }
  }
  FX_LOGS(WARNING) << "missing '\"'";
  return false;
}

Input Dictionary::AsInput() const {
  std::ostringstream oss;
  size_t num_keys = 0;
  for (uint16_t level = 0; level <= max_level_; ++level) {
    const auto words = words_by_level_.find(level);
    if (words == words_by_level_.end()) {
      continue;
    }
    for (const auto& word : words->second) {
      oss << "key" << ++num_keys;
      if (level) {
        oss << "@" << level;
      }
      oss << "=\"";
      for (auto c : word) {
        if (c == '\\') {
          oss << "\\\\";
        } else if (c == '"') {
          oss << "\\\"";
        } else if (isprint(c) || isspace(c)) {
          oss << char(c);
        } else {
          oss << "\\x" << hex_byte << int(c);
          oss << std::dec;
        }
      }
      oss << "\"\n";
    }
  }
  return Input(oss.str());
}

void Dictionary::ForEachWord(fit::function<void(const uint8_t*, size_t)> func) const {
  FX_DCHECK(options_);
  for (uint16_t level = 0; level <= options_->dictionary_level(); ++level) {
    const auto words = words_by_level_.find(level);
    if (words == words_by_level_.end()) {
      continue;
    }
    for (const auto& word : words->second) {
      func(word.data(), word.size());
    }
  }
}

}  // namespace fuzzing
