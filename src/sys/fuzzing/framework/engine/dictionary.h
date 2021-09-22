// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_DICTIONARY_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_DICTIONARY_H_

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

class Dictionary final {
 public:
  using Word = std::vector<uint8_t>;
  using Level = std::vector<Word>;

  Dictionary() = default;
  Dictionary(Dictionary&& other) noexcept { *this = std::move(other); }
  ~Dictionary() = default;
  Dictionary& operator=(Dictionary&& other) noexcept;

  // Sets options.
  void Configure(const std::shared_ptr<Options>& options);

  // Adds a word to this dictionary.
  void Add(const uint8_t* data, size_t size, uint16_t level = 0);
  void Add(Word&& word, uint16_t level = 0);

  // Resets the dictionary to an initial state and attempts to interpret the given input as a
  // dictionary. Invalid entries are skipped.
  bool Parse(const Input& input);

  // Writes the dictionary out to an input.
  Input AsInput() const;

  // Apply |func| to each word in the dictionary with a level at or below the previously
  // |Configure|d dictionary level (default is 0).
  void ForEachWord(fit::function<void(const uint8_t*, size_t)> func) const;

 private:
  // Parses |str| as a dictionary level, which is an unsigned number. Returns false if |str| is not
  // a valid number. Otherwise returns true and the parsed level via |out_level|.
  bool ParseLevel(const std::string& str, uint16_t* out_level);

  // Parse |str| as a word, which may contain sequences like \\, \", or \xNN where N is a hex digit.
  // Returns false if the value is empty or contains invalid escape sequences (e.g. \x5G).
  // Otherwise, returns true, the parsed word via |out_word|, and the portion of |str| that was not
  // parsed in |out_remaining|.
  bool ParseWord(const std::string& str, Word* out_word, std::string* out_remaining);

  // Parses |str| as a number with the given |base|, e.g. 10 or 16. Returns false if |str| is not a
  // number of if it cannot be expressed as a value of type |T|. Otherwise, returns true and the
  // parsed value via |out|.
  template <typename T>
  bool ParseNumber(const std::string& str, int base, T* out) {
    const char* c_str = str.c_str();
    char* endptr;
    auto u64 = std::strtoul(c_str, &endptr, 10);
    if (c_str == endptr || *endptr != '\0' || u64 > std::numeric_limits<T>::max()) {
      return false;
    }
    *out = static_cast<T>(u64);
    return true;
  }

  std::shared_ptr<Options> options_;
  std::unordered_map<uint16_t, Level> words_by_level_;
  uint16_t max_level_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(Dictionary);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_DICTIONARY_H_
