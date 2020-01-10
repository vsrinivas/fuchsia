// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/experimental_flags.h"

#include <set>

namespace fidl {

bool ExperimentalFlags::SetFlagByName(const std::string_view flag) {
  auto it = FLAG_STRINGS.find(flag);
  if (it == FLAG_STRINGS.end()) {
    return false;
  }
  SetFlag(it->second);
  return true;
}

void ExperimentalFlags::SetFlag(Flag flag) { flags.insert(flag); }

bool ExperimentalFlags::IsFlagEnabled(Flag flag) const { return flags.find(flag) != flags.end(); }

std::map<const std::string_view, const ExperimentalFlags::Flag> ExperimentalFlags::FLAG_STRINGS = {
    {"enable_handle_rights", Flag::kEnableHandleRights},
};

}  // namespace fidl
