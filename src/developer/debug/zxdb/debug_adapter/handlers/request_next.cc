// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_next.h"

#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

void OnRequestNext(DebugAdapterContext* ctx, const dap::NextRequest& request,
                   std::function<void(dap::ResponseOrError<dap::NextResponse>)> callback) {
  auto thread = ctx->GetThread(request.threadId);

  if (Err err = ctx->CheckStoppedThread(thread); err.has_error()) {
    callback(dap::Error(err.msg()));
    return;
  }

  // TODO(69411): Add support for instruction step mode when request specifies that granularity.
  auto controller = std::make_unique<StepOverThreadController>(StepMode::kSourceLine);

  thread->ContinueWith(std::move(controller), [callback](const Err& err) {
    if (err.has_error()) {
      callback(dap::Error("Next command failed!"));
      return;
    }
    callback(dap::NextResponse());
  });
}

}  // namespace zxdb
