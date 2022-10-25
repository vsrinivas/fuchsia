// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_UTIL_SCHEDULER_PROFILE_H_
#define SRC_UI_SCENIC_LIB_SCENIC_UTIL_SCHEDULER_PROFILE_H_

#include <lib/zx/thread.h>

#include <string>

namespace util {

// Sets the scheduler role of the given thread.
//
zx_status_t SetSchedulerRole(const zx::unowned_thread& thread, const std::string& role);

}  // namespace util

#endif  // SRC_UI_SCENIC_LIB_SCENIC_UTIL_SCHEDULER_PROFILE_H_
