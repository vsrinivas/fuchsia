// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbol-index/reader.h"

#include <string_view>

#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/trim.h"

namespace symbol_index {

Error Reader::Read(std::istream& input, const std::string& input_name,
                             std::vector<std::vector<std::string>>* result) {
  if (input.fail()) {
    return fxl::StringPrintf("Cannot open %s to read", input_name.c_str());
  }

  while (!input.eof()) {
    std::string line;
    std::vector<std::string> items;

    std::getline(input, line);
    if (input.fail()) {
      // If the input ends with \n, we will get failbit, eofbit and line == "".
      if (input.eof())
        break;
      return fxl::StringPrintf("Error reading %s", input_name.c_str());
    }

    std::string_view trimed = fxl::TrimString(line, " \t\r\n");

    // Ignores empty lines and lines starting with #, which are considered comments.
    if (trimed.empty() || trimed[0] == '#')
      continue;

    result->push_back(fxl::SplitStringCopy(trimed, std::string(1, column_separator_),
                                           fxl::kKeepWhitespace, fxl::kSplitWantNonEmpty));
  }

  return "";
}

}  // namespace symbol_index
