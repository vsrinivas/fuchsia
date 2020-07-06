// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_UTIL_H_

#include <lib/fidl/walker.h>

// Clear the ownership bit in a vector/string count.
#define CLEAR_COUNT_OWNERSHIP_BIT(x) ((x) & (~fidl::internal::kVectorOwnershipMask))
// Clear the ownership bit used by tracking_ptr.
#define CLEAR_PTR_OWNERSHIP_BIT(x)                                \
  (reinterpret_cast<decltype(x)>(reinterpret_cast<uintptr_t>(x) & \
                                 (~fidl::internal::kNonArrayTrackingPtrOwnershipMask)))

#endif  // SRC_TESTS_BENCHMARKS_FIDL_REFERENCE_UTIL_H_
