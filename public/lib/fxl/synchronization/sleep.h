// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_SYNCHRONIZATION_SLEEP_H_
#define LIB_FXL_SYNCHRONIZATION_SLEEP_H_

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

namespace fxl {

FXL_EXPORT void SleepFor(TimeDelta duration);

}  // namespace fxl

#endif  // LIB_FXL_SYNCHRONIZATION_SLEEP_H_
