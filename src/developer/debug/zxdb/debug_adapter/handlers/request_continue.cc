// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_continue.h"

#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

dap::ResponseOrError<dap::ContinueResponse> OnRequestContinue(DebugAdapterContext* ctx,
                                                              const dap::ContinueRequest& request) {
  dap::ContinueResponse response = {};
  auto thread = ctx->GetThread(request.threadId);

  // TODO(69389): Currently only handling continuing threads. Continuing the entire process is TBD.
  if (!thread) {
    return dap::Error("Invalid thread ID");
  }

  // Continue without enabling forward exceptions.
  thread->Continue(false);
  response.allThreadsContinued = false;
  return response;
}

}  // namespace zxdb
