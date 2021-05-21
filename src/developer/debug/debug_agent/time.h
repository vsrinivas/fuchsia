// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TIME_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TIME_H_

#include <stdint.h>

#include <chrono>

namespace debug_agent {

// Used for cross-platform deadlines. The steady_clock is the same as a zx::time (this is
// verified in the tests).
//
// To get the current time:
//
//    TickTimePoint now = std::chrono::steady_clock::now();
//
// The equivalent of zx::deadline_after is:
//
//    TickTimePoint deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10)
//
// To convert to Zircon types:
//
//    zx_time ticks = time_tick_point.time_since_epoch().count()
//    zx::time time(time_tick_point.time_since_epoch().count());
//
using TickTimePoint = std::chrono::time_point<std::chrono::steady_clock>;

// Returns the current time as a timestamp for use in IPC messages.
inline uint64_t GetNowTimestamp() {
  return std::chrono::steady_clock::now().time_since_epoch().count();
}

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TIME_H_
