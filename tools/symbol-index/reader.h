// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOL_INDEX_READER_H_
#define TOOLS_SYMBOL_INDEX_READER_H_

#include <istream>
#include <string>
#include <vector>

#include "tools/symbol-index/error.h"

namespace symbol_index {

// Reads a text input stream line by line and separates each line with the given separator.
// Blank lines and comments are ignored.
class Reader {
 public:
  explicit Reader(char column_separator) : column_separator_(column_separator) {}

  Error Read(std::istream& input, const std::string& input_name,
             std::vector<std::vector<std::string>>* result);

 private:
  char column_separator_;
};

}  // namespace symbol_index

#endif  // TOOLS_SYMBOL_INDEX_READER_H_
