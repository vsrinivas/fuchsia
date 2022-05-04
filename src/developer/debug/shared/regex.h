// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_REGEX_H_
#define SRC_DEVELOPER_DEBUG_SHARED_REGEX_H_

#include <memory>
#include <string>

#include <re2/re2.h>

#include "src/lib/fxl/macros.h"

namespace debug {

// Simple class wrapper over RE2.
//
// Currently it only looks for normal matches, but can be extended to support capturing and other
// neat regex stuff.
class Regex {
 public:
  enum class CompareType {
    kCaseSensitive,
    kCaseInsensitive,
  };

  bool valid() const { return regex_ != nullptr; }

  bool Init(const std::string& regexp, CompareType = CompareType::kCaseInsensitive);
  bool Match(const std::string&) const;

 private:
  std::unique_ptr<re2::RE2> regex_;
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_REGEX_H_
