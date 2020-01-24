// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_UTIL_SCHEDULER_PROFILE_H_
#define SRC_UI_SCENIC_LIB_SCENIC_UTIL_SCHEDULER_PROFILE_H_

#include <lib/zx/profile.h>
#include <lib/zx/time.h>

namespace util {

// Returns a handle to a scheduler profile for the specified deadline parameters.
zx::profile GetSchedulerProfile(zx::duration capacity, zx::duration deadline, zx::duration period);

}  // namespace util

#endif  // SRC_UI_SCENIC_LIB_SCENIC_UTIL_SCHEDULER_PROFILE_H_
