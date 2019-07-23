// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/deprecated/lib/vocabulary/once_fn.h"
#include "src/connectivity/overnet/deprecated/lib/vocabulary/slice.h"

namespace overnet {

class TimeStamp;

struct LazySliceArgs {
  const Border desired_border;
  const size_t max_length;
  const bool has_other_content = false;
};

using LazySlice = OnceFn<Slice(LazySliceArgs), 32 * sizeof(void*)>;

}  // namespace overnet
