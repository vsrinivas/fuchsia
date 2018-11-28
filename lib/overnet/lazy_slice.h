// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "once_fn.h"
#include "slice.h"

namespace overnet {

class TimeStamp;

struct LazySliceArgs {
  size_t desired_prefix;
  size_t max_length;
  bool has_other_content = false;
  TimeStamp* delay_until_time = nullptr;
};

using LazySlice = OnceFn<Slice(LazySliceArgs), 32 * sizeof(void*)>;

}  // namespace overnet
