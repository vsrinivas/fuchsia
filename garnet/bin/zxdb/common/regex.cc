// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/common/regex.h"

#include "lib/fxl/logging.h"

namespace zxdb {

namespace {

std::string GetRegexError(const regex_t& r, int status) {
  char err_buf[256];
  regerror(status, &r, err_buf, sizeof(err_buf));
  return err_buf;
}

}  // namespace

Regex::Regex() = default;
Regex::~Regex() {
  if (handle_.has_value())
    regfree(&handle_.value());
}

Err Regex::Init(const std::string& regexp, Regex::CompareType compare_type) {
  if (valid())
    return Err("Already initialized.");

  regex_t r;
  int flags = REG_EXTENDED;
  if (compare_type == Regex::CompareType::kCaseInsensitive)
    flags |= REG_ICASE;
  int status = regcomp(&r, regexp.c_str(), flags);
  if (status) {
    auto regex_err = GetRegexError(r, status);
    return Err("Could not compile regexp: %s", regex_err.c_str());
  }

  handle_ = r;
  return Err();
}

bool Regex::Match(const std::string& candidate) const {
  FXL_DCHECK(valid());

  int status = regexec(&handle_.value(), candidate.c_str(), 0, nullptr, 0);
  return status == 0;
}

}  // namespace zxdb
