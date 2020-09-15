// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/experimental_flags.h"

namespace fidl {

bool ExperimentalFlags::SetFlagByName(const std::string_view flag) {
  auto it = FLAG_STRINGS.find(flag);
  if (it == FLAG_STRINGS.end()) {
    return false;
  }
  SetFlag(it->second);
  return true;
}

void ExperimentalFlags::SetFlag(Flag flag) { flags_ |= static_cast<FlagSet>(flag); }

bool ExperimentalFlags::IsFlagEnabled(Flag flag) const {
  return (flags_ & static_cast<FlagSet>(flag)) != 0;
}

std::map<const std::string_view, const ExperimentalFlags::Flag> ExperimentalFlags::FLAG_STRINGS = {
    {"enable_handle_rights", Flag::kEnableHandleRights},
    {"disallow_old_handle_syntax", Flag::kDisallowOldHandleSyntax},
};

}  // namespace fidl
