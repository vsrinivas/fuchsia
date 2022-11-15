// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_evaluate.h"

#include "dap/session.h"
#include "src/developer/debug/zxdb/console/command_context.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

void OnRequestEvaluate(
    DebugAdapterContext* ctx, const dap::EvaluateRequest& req,
    const std::function<void(dap::ResponseOrError<dap::EvaluateResponse>)>& callback) {
  // Ignore requests with no context
  if (!req.context.has_value()) {
    callback(dap::Error());
    return;
  }

  // Utilize the console for REPL context.
  if (req.context.value() == "repl") {
    ctx->console()->ProcessInputLine(
        req.expression,
        fxl::MakeRefCounted<OfflineCommandContext>(
            ctx->console(), [cb = callback](OutputBuffer output, std::vector<Err> errors) {
              dap::EvaluateResponse resp;
              resp.result = output.AsString();
              cb(resp);
            }));
  } else {
    callback(dap::Error());
  }
}

}  // namespace zxdb
