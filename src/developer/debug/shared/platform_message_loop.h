// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_PLATFORM_MESSAGE_LOOP_H_
#define SRC_DEVELOPER_DEBUG_SHARED_PLATFORM_MESSAGE_LOOP_H_

#if defined(__Fuchsia__)
#include "src/developer/debug/shared/message_loop_target.h"
#else
#include "src/developer/debug/shared/message_loop_poll.h"
#endif

namespace debug_ipc {

#if defined(__Fuchsia__)
using PlatformMessageLoop = MessageLoopTarget;
#else
using PlatformMessageLoop = MessageLoopPoll;
#endif

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_PLATFORM_MESSAGE_LOOP_H_
