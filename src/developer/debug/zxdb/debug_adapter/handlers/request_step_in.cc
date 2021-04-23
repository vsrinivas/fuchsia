// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_step_in.h"

#include "src/developer/debug/zxdb/client/step_into_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

void OnRequestStepIn(DebugAdapterContext* ctx, const dap::StepInRequest& request,
                     std::function<void(dap::ResponseOrError<dap::StepInResponse>)> callback) {
  auto thread = ctx->GetThread(request.threadId);

  if (Err err = ctx->CheckStoppedThread(thread); err.has_error()) {
    callback(dap::Error(err.msg()));
    return;
  }

  auto controller = std::make_unique<StepIntoThreadController>(StepMode::kSourceLine);

  thread->ContinueWith(std::move(controller), [callback](const Err& err) {
    if (err.has_error()) {
      callback(dap::Error("Step-in command failed!"));
      return;
    }
    callback(dap::StepInResponse());
  });
}

}  // namespace zxdb
