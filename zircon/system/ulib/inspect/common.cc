// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/common.h>

#include <atomic>

static std::atomic_uint_fast64_t unique_name_id;

__EXPORT uint64_t inspect_counter_increment(uint64_t counter_id) {
  switch (counter_id) {
    case kUniqueNameCounterId:
      return unique_name_id.fetch_add(1, std::memory_order_relaxed);
    default:
      return 0xFFFFFFFFFFFFFFFFULL;
  };
}

__EXPORT void inspect_counter_reset(uint64_t counter_id) {
  switch (counter_id) {
    case kUniqueNameCounterId:
      unique_name_id = 0;
      break;
    default:
      return;
  };
}
