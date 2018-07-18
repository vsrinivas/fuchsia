// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/future.h>

namespace modular {

namespace internal {

ResultCollector<Future<>>::ResultCollector(size_t reserved_count)
    : reserved_count_(reserved_count) {}

bool ResultCollector<Future<>>::IsComplete() const {
  return finished_count_ == reserved_count_;
}

void ResultCollector<Future<>>::AssignResult(size_t) { finished_count_++; }

void ResultCollector<Future<>>::Complete(Future<>* future) const {
  future->Complete();
}

}  // namespace internal

}  // namespace modular