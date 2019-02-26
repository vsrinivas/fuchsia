// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_SYNC_HELPER_MUTABLE_H_
#define SRC_LEDGER_BIN_SYNC_HELPER_MUTABLE_H_

#include <utility>

namespace ledger {

// This class wraps a type A so that a const instance of Mutable<A> can still
// mutate the internal A object.
// It is used to capture a mutable variable in a non mutable lambda:
// [b = Mutable(false)] {
//   *b = true;
// }
template <typename A>
class Mutable {
 public:
  explicit Mutable(A value) : value_(std::move(value)) {}
  Mutable(Mutable&&) = default;
  Mutable<A>& operator=(Mutable&&) = default;

  A& operator*() const { return value_; }

  A* operator->() const { return &value_; }

 private:
  mutable A value_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_SYNC_HELPER_MUTABLE_H_
