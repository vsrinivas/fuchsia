// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace-test-utils/squelch.h"

#include <fbl/string_buffer.h>

namespace trace_testing {

std::unique_ptr<Squelcher> Squelcher::Create(
    const std::vector<std::pair<std::string_view, std::string_view>>& replacements) {
  return std::unique_ptr<Squelcher>(new Squelcher(replacements));
}

Squelcher::Squelcher(
    const std::vector<std::pair<std::string_view, std::string_view>>& replacements) {
  for (auto& r : replacements) {
    compiled_replacements_.emplace_back(
        std::make_pair(std::make_unique<re2::RE2>(r.first), r.second));
  }
}

fbl::String Squelcher::Squelch(const char* raw_str) {
  std::string str(raw_str);
  for (auto& r : compiled_replacements_) {
    re2::RE2::GlobalReplace(&str, *r.first, r.second);
  }
  return fbl::String(str);
}

}  // namespace trace_testing
