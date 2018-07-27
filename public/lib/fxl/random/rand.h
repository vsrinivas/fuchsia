// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_RANDOM_RAND_H_
#define LIB_FXL_RANDOM_RAND_H_

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "lib/fxl/fxl_export.h"

namespace fxl {

// Returns a random number in range [0, UINT64_MAX]
FXL_EXPORT uint64_t RandUint64();

}  // namespace fxl

#endif  // LIB_FXL_RANDOM_RAND_H_
