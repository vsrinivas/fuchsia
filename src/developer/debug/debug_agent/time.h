// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TIME_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TIME_H_

#include <stdint.h>

namespace debug_agent {

// Returns the current time as a timestamp for use in IPC messages.
uint64_t GetNowTimestamp();

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TIME_H_
