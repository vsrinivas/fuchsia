// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_step_out.h"

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

void OnRequestStepOut(DebugAdapterContext* ctx, const dap::StepOutRequest& request,
                      std::function<void(dap::ResponseOrError<dap::StepOutResponse>)> callback) {
  auto thread = ctx->GetThread(request.threadId);

  if (Err err = ctx->CheckStoppedThread(thread); err.has_error()) {
    callback(dap::Error(err.msg()));
    return;
  }

  auto controller = std::make_unique<FinishThreadController>(thread->GetStack(), 0);

  thread->ContinueWith(std::move(controller), [callback](const Err& err) {
    if (err.has_error()) {
      callback(dap::Error("Step-out command failed!"));
      return;
    }
    callback(dap::StepOutResponse());
  });
}

}  // namespace zxdb
