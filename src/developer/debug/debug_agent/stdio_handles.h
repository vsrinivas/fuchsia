// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_STDIO_HANDLES_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_STDIO_HANDLES_H_

#include <lib/zx/socket.h>

#include "src/developer/debug/shared/buffered_zx_socket.h"

namespace debug_agent {

using OwnedStdioHandle = zx::socket;
using BufferedStdioHandle = debug::BufferedZxSocket;

// The handles given to a launched process or component.
//
// We can add stdin in the future if we have a need.
struct StdioHandles {
  OwnedStdioHandle out;
  OwnedStdioHandle err;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_STDIO_HANDLES_H_
