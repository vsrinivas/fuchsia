// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_SYNCHRONIZATION_SLEEP_H_
#define LIB_FTL_SYNCHRONIZATION_SLEEP_H_

#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"

namespace ftl {

void SleepFor(TimeDelta duration);

}  // namespace ftl

#endif  // LIB_FTL_SYNCHRONIZATION_SLEEP_H_
