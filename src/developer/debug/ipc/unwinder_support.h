// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_UNWINDER_SUPPORT_H_
#define SRC_DEVELOPER_DEBUG_IPC_UNWINDER_SUPPORT_H_

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/unwinder/unwind.h"

namespace debug_ipc {

std::vector<debug_ipc::StackFrame> ConvertFrames(const std::vector<unwinder::Frame>& frames);

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_UNWINDER_SUPPORT_H_
