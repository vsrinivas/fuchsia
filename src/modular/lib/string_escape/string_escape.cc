// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/string_escape/string_escape.h"

using fxl::StringView;

namespace modular {

std::string StringEscape(StringView input, StringView chars_to_escape, char escape_char) {
  std::string output;
  output.reserve(input.size());

  for (const auto& c : input) {
    if (chars_to_escape.find(c) != StringView::npos || c == escape_char) {
      output.push_back(escape_char);
    }
    output.push_back(c);
  }

  return output;
}

std::string StringUnescape(StringView input, char escape_char) {
  std::string output;

  for (size_t i = 0; i < input.size(); i++) {
    if (input[i] == escape_char) {
      FXL_DCHECK(i != input.size() - 1) << "StringUnescape: unescapable string: " << input;
      if (i != input.size() - 1) {
        i++;
      }
    }
    output.push_back(input[i]);
  }

  return output;
}

std::vector<StringView> SplitEscapedString(StringView input, char split_char, char escape_char) {
  std::vector<StringView> output;
  size_t last_pos = 0;
  for (size_t i = 0; i < input.size(); i++) {
    if (input[i] == escape_char) {
      i++;
      // skips a 2nd time:
      continue;
    }

    if (input[i] == split_char) {
      output.push_back(input.substr(last_pos, i - last_pos));
      last_pos = i + 1;
      continue;
    }
  }

  if (last_pos < input.size()) {
    output.push_back(input.substr(last_pos, input.size() - last_pos));
  }

  return output;
}

}  // namespace modular
