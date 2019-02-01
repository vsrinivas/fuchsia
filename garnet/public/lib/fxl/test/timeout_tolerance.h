// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/time/time_delta.h"

#ifndef LIB_FXL_TEST_TIMEOUT_TOLERANCE_H_
#define LIB_FXL_TEST_TIMEOUT_TOLERANCE_H_

namespace fxl {
constexpr TimeDelta kTimeoutTolerance = TimeDelta::Zero();
}  // namespace fxl

#endif  // LIB_FXL_TEST_TIMEOUT_TOLERANCE_H_
