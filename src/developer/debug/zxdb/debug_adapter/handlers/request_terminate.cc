// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_terminate.h"

#include <dap/session.h>

#include "src/developer/debug/zxdb/console/command_context.h"

namespace zxdb {

void OnRequestTerminate(
    DebugAdapterContext* ctx, const dap::TerminateRequest& req,
    const std::function<void(dap::ResponseOrError<dap::TerminateResponse>)>& callback) {
  callback(dap::TerminateResponse());
  ctx->console()->Quit();
}

}  // namespace zxdb
