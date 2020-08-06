// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_suspend_handle.h"

#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

ZirconSuspendHandle::ZirconSuspendHandle(zx::suspend_token token, zx_koid_t thread_koid)
    : token_(std::move(token)), thread_koid_(thread_koid) {
  DEBUG_LOG(Thread) << "[T: " << thread_koid_ << "] Creating suspend handle.";
}

ZirconSuspendHandle::~ZirconSuspendHandle() {
  DEBUG_LOG(Thread) << "[T: " << thread_koid_ << "] Closing suspend handle.";
}

}  // namespace debug_agent
