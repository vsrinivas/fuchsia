// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_threads.h"

#include "src/developer/debug/zxdb/client/breakpoint_location.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

dap::ResponseOrError<dap::ThreadsResponse> OnRequestThreads(DebugAdapterContext *ctx,
                                                            const dap::ThreadsRequest &req) {
  dap::ThreadsResponse response = {};
  auto process = ctx->GetCurrentProcess();
  if (process) {
    auto threads = process->GetThreads();
    for (auto t : threads) {
      dap::Thread thread_info;
      thread_info.id = t->GetKoid();
      thread_info.name = t->GetName();
      response.threads.push_back(thread_info);
    }
  }

  return response;
}

}  // namespace zxdb
