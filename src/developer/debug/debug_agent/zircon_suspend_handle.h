// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_SUSPEND_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_SUSPEND_HANDLE_H_

#include <lib/zx/suspend_token.h>

#include "src/developer/debug/debug_agent/suspend_handle.h"

namespace debug_agent {

class ZirconSuspendHandle : public SuspendHandle {
 public:
  ZirconSuspendHandle(zx::suspend_token token, zx_koid_t thread_koid);
  ~ZirconSuspendHandle() override;

 private:
  zx::suspend_token token_;
  zx_koid_t thread_koid_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_SUSPEND_HANDLE_H_
