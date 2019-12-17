// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_TEST_UTIL_H_
#define SRC_UI_SCENIC_LIB_UTILS_TEST_UTIL_H_

#include <lib/zx/event.h>

#include "src/lib/fxl/time/time_delta.h"

namespace utils {
namespace test {

// Synchronously checks whether the event has signalled any of the bits in
// |signal|.
bool IsEventSignalled(const zx::event& event, zx_signals_t signal);

// Create a duplicate of the event.
zx::event CopyEvent(const zx::event& event);

}  // namespace test
}  // namespace utils

#endif  // SRC_UI_SCENIC_LIB_UTILS_TEST_UTIL_H_
