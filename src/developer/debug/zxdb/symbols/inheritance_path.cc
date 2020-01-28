// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/inheritance_path.h"

namespace zxdb {

std::optional<uint32_t> InheritancePath::BaseOffsetInDerived() const {
  uint32_t result = 0;
  // Skip path_[0] because there's no InheritedFrom class to get from a class to itself.
  for (size_t i = 1; i < path_.size(); i++) {
    if (path_[i].from->kind() == InheritedFrom::kConstant) {
      result += path_[i].from->offset();
    } else {
      return std::nullopt;  // Non-constant can't compute a simple offset.
    }
  }
  return result;
}

}  // namespace zxdb
