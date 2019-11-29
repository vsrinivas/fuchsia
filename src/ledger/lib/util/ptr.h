// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_UTIL_PTR_H_
#define SRC_LEDGER_LIB_UTIL_PTR_H_

#include <memory>

namespace ledger {

// Returns true if the provided std::unique_ptrs are both empty, or point to
// equal objects. Returns false otherwise.
template <typename T>
bool EqualPtr(const std::unique_ptr<T>& lhs, const std::unique_ptr<T>& rhs) {
  if (bool(lhs) != bool(rhs)) {
    return false;
  }
  if (!lhs && !rhs) {
    return true;
  }
  return *lhs == *rhs;
}

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_UTIL_PTR_H_
