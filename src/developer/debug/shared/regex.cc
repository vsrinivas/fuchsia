// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/regex.h"

#include <lib/syslog/cpp/macros.h>

namespace debug {

bool Regex::Init(const std::string& regexp_str, Regex::CompareType compare_type) {
  if (valid())
    return false;

  re2::RE2::Options opts(re2::RE2::DefaultOptions);
  opts.set_case_sensitive(compare_type == Regex::CompareType::kCaseSensitive);

  auto r = std::make_unique<re2::RE2>(regexp_str, opts);
  if (!r->ok()) {
    return false;
  }

  regex_ = std::move(r);
  return true;
}

bool Regex::Match(const std::string& candidate) const {
  FX_DCHECK(valid());
  return re2::RE2::PartialMatch(candidate, *regex_);
}

}  // namespace debug
