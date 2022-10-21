// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/experimental_flags.h"

namespace fidl {

bool ExperimentalFlags::EnableFlagByName(const std::string_view flag) {
  auto it = FLAG_STRINGS.find(flag);
  if (it == FLAG_STRINGS.end()) {
    return false;
  }
  EnableFlag(it->second);
  return true;
}

void ExperimentalFlags::EnableFlag(Flag flag) { flags_ |= static_cast<FlagSet>(flag); }

bool ExperimentalFlags::IsFlagEnabled(Flag flag) const {
  return (flags_ & static_cast<FlagSet>(flag)) != 0;
}

void ExperimentalFlags::ForEach(
    const fit::function<void(const std::string_view, Flag, bool)>& fn) const {
  for (const auto& [name, flag] : FLAG_STRINGS) {
    bool is_enabled = IsFlagEnabled(flag);
    fn(name, flag, is_enabled);
  }
}

std::map<const std::string_view, const ExperimentalFlags::Flag> ExperimentalFlags::FLAG_STRINGS = {
    {"unknown_interactions", Flag::kUnknownInteractions},
    {"no_optional_structs", Flag::kNoOptionalStructs},
    {"allow_new_types", Flag::kAllowNewTypes},
    {"allow_overflowing", Flag::kAllowOverflowing},
    {"output_index_json", Flag::kOutputIndexJson},
    {"zx_c_types", Flag::kZxCTypes},
    {"simple_empty_response_syntax", Flag::kSimpleEmptyResponseSyntax},
};

}  // namespace fidl
