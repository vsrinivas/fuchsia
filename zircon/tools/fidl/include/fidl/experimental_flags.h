// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_EXPERIMENTAL_FLAGS_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_EXPERIMENTAL_FLAGS_H_

#include <map>
#include <string_view>
#include <unordered_set>

namespace fidl {

class ExperimentalFlags {
 public:
  enum class Flag { kEnableHandleRights };

  bool SetFlagByName(const std::string_view flag);
  void SetFlag(Flag flag);

  bool IsFlagEnabled(Flag flag) const;

 private:
  static std::map<const std::string_view, const Flag> FLAG_STRINGS;

  std::unordered_set<Flag> flags;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_EXPERIMENTAL_FLAGS_H_
