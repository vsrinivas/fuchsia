// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/inheritance_path.h"

#include "src/lib/fxl/logging.h"

namespace zxdb {

InheritancePath::InheritancePath(fxl::RefPtr<Collection> derived, fxl::RefPtr<InheritedFrom> from,
                                 fxl::RefPtr<Collection> base) {
  path_.emplace_back(std::move(derived));
  path_.emplace_back(std::move(from), std::move(base));
}

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

InheritancePath InheritancePath::SubPath(size_t begin_index, size_t len) const {
  FXL_DCHECK(len != 0);
  FXL_DCHECK(begin_index >= 0 && begin_index < path_.size());
  FXL_DCHECK(len == kToEnd || begin_index + len <= path_.size());

  InheritancePath result;
  result.path_.insert(result.path_.begin(), path_.begin() + begin_index,
                      len == kToEnd ? path_.end() : path_.begin() + (begin_index + len));

  // The first element of the result shouldn't have a "from" since it's not coming from anywhere.
  result.path_.front().from.reset();

  return result;
}

}  // namespace zxdb
