// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/frame_impl.h"

#include "garnet/bin/zxdb/client/thread_impl.h"

namespace zxdb {

FrameImpl::FrameImpl(ThreadImpl* thread,
                     const debug_ipc::StackFrame& stack_frame)
    : Frame(thread->session()), thread_(thread), stack_frame_(stack_frame) {
}

FrameImpl::~FrameImpl() = default;

Thread* FrameImpl::GetThread() const {
  return thread_;
}

uint64_t FrameImpl::GetIP() const {
  return stack_frame_.ip;
}

}  // namespace zxdb
