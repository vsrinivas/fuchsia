// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/regex.h"

#include "lib/fxl/logging.h"

namespace debug_ipc {

Regex::Regex() = default;

Regex::~Regex() {
  if (handle_.has_value())
    regfree(&handle_.value());
}

Regex::Regex(Regex&& other) : handle_(std::move(other.handle_)) {
  other.handle_.reset();
}

Regex& Regex::operator=(Regex&& other) {
  if (this == &other)
    return *this;

  handle_ = std::move(other.handle_);
  other.handle_.reset();
  return *this;
}

bool Regex::Init(const std::string& regexp, Regex::CompareType compare_type) {
  if (valid())
    return false;

  regex_t r;
  int flags = REG_EXTENDED;
  if (compare_type == Regex::CompareType::kCaseInsensitive)
    flags |= REG_ICASE;
  int status = regcomp(&r, regexp.c_str(), flags);
  if (status) {
    return false;
  }

  handle_ = r;
  return true;
}

bool Regex::Match(const std::string& candidate) const {
  FXL_DCHECK(valid());

  int status = regexec(&handle_.value(), candidate.c_str(), 0, nullptr, 0);
  return status == 0;
}

}  // namespace debug_ipc
