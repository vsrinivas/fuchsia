// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_threads.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

dap::ResponseOrError<dap::ThreadsResponse> OnRequestThreads(DebugAdapterContext* ctx,
                                                            const dap::ThreadsRequest& req) {
  dap::ThreadsResponse response = {};
  auto targets = ctx->session()->system().GetTargets();
  for (auto target : targets) {
    if (!target) {
      continue;
    }
    auto process = target->GetProcess();
    if (!process) {
      continue;
    }
    auto threads = process->GetThreads();
    for (auto thread : threads) {
      dap::Thread thread_info;
      thread_info.id = thread->GetKoid();
      thread_info.name = thread->GetName();
      response.threads.push_back(thread_info);
    }
  }

  return response;
}

}  // namespace zxdb
