// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_pause.h"

#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

void OnRequestPause(DebugAdapterContext* ctx, const dap::PauseRequest& request,
                    std::function<void(dap::ResponseOrError<dap::PauseResponse>)> callback) {
  auto thread = ctx->GetThread(request.threadId);

  // TODO(69404): Currently only handling pausing threads. Pausing the entire process is TBD.
  if (!thread) {
    callback(dap::Error("Invalid thread ID"));
    return;
  }

  thread->Pause([weak_thread = thread->GetWeakPtr(), callback, ctx]() {
    if (!weak_thread) {
      callback(dap::Error("Thread exited!"));
      return;
    }

    // Send stopped event with reason "pause"
    dap::StoppedEvent event;
    event.reason = "pause";
    event.threadId = weak_thread->GetKoid();
    ctx->dap().send(event);

    callback(dap::PauseResponse());
  });
}

}  // namespace zxdb
