// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_EXPERIMENTAL_FLAGS_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_EXPERIMENTAL_FLAGS_H_

#include <lib/fit/function.h>

#include <map>
#include <string_view>

namespace fidl {

class ExperimentalFlags {
 public:
  using FlagSet = uint32_t;
  enum class Flag : FlagSet {
    // TODO(fxbug.dev/106641): Allows backends to implement overflowing experiments.
    kAllowOverflowing = 0b10,
    kAllowNewTypes = 0b100,
    kUnknownInteractions = 0b10000,
    kNoOptionalStructs = 0b100000,
    kOutputIndexJson = 0b1000000,

    // TODO(fxbug.dev/110021): A temporary measure describe in
    // fxbug.dev/110294.
    kZxCTypes = 0b10000000,
  };

  ExperimentalFlags() = default;
  explicit ExperimentalFlags(Flag flag) : flags_(static_cast<FlagSet>(flag)) {}

  bool EnableFlagByName(const std::string_view flag);
  void EnableFlag(Flag flag);

  bool IsFlagEnabled(Flag flag) const;
  void ForEach(const fit::function<void(const std::string_view, Flag, bool)>& fn) const;

 private:
  static std::map<const std::string_view, const Flag> FLAG_STRINGS;

  FlagSet flags_{0};
};

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_EXPERIMENTAL_FLAGS_H_
