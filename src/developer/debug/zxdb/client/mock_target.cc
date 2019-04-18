// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/mock_target.h"

#include "src/lib/fxl/logging.h"

namespace zxdb {

void MockTarget::SetRunningProcess(Process* process) {
  state_ = kRunning;
  process_ = process;
}

void MockTarget::Launch(Callback callback) { FXL_NOTREACHED(); }

void MockTarget::Kill(Callback callback) { FXL_NOTREACHED(); }

void MockTarget::Attach(uint64_t koid, Callback callback) { FXL_NOTREACHED(); }

void MockTarget::Detach(Callback callback) { FXL_NOTREACHED(); }

void MockTarget::OnProcessExiting(int return_code) { FXL_NOTREACHED(); }

}  // namespace zxdb
