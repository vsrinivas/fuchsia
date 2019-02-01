// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <regex.h>

#include <optional>
#include <string>

#include "garnet/bin/zxdb/common/err.h"
#include "lib/fxl/macros.h"

namespace zxdb {

// Simple RAII class wrapper over the POSIX regex API.
// Currently it only looks for normal matches, but can be extended to support
// capturing and other neat regex stuff.
class Regex {
 public:
  enum class CompareType {
    kCaseSensitive,
    kCaseInsensitive,
  };

  Regex();
  ~Regex();

  Err Init(const std::string& regexp,
           CompareType = CompareType::kCaseInsensitive);
  bool Match(const std::string&) const;

  FXL_DISALLOW_COPY_AND_ASSIGN(Regex);

  bool valid() const { return handle_.has_value(); }

 private:
  // Optional so we can mark when a regex is not compiled.
  std::optional<regex_t> handle_ = std::nullopt;
};

}  // namespace zxdb

