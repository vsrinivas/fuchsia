// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_threads.h"

namespace zxdb {

dap::StackTraceResponse PopulateStackTraceResponse(DebugAdapterContext* ctx, Thread* thread,
                                                   const dap::StackTraceRequest& req) {
  dap::StackTraceResponse response;
  auto& stack = thread->GetStack();
  int start_frame = 0;
  int total_frames = stack.size();

  // Return frames starting from requested level if specified
  if (req.startFrame) {
    start_frame = req.startFrame.value();
  }

  // Return frames upto requested levels if specified
  if (req.levels && (total_frames > req.levels.value())) {
    total_frames = req.levels.value();
  }

  auto file_provider = SourceFileProviderImpl(thread->GetProcess()->GetTarget()->settings());
  for (auto i = start_frame; i < (start_frame + total_frames); i++) {
    dap::StackFrame frame;
    auto location = stack[i]->GetLocation();

    // Try to get the source path.
    auto data_or =
        file_provider.GetFileData(location.file_line().file(), location.file_line().comp_dir());
    if (!data_or.has_error()) {
      dap::Source source;
      source.path = data_or.value().full_path;
      frame.source = source;
    }

    frame.line = location.file_line().line();
    frame.column = location.column();
    frame.name = location.symbol().Get()->GetFullName();
    frame.id = ctx->IdForFrame(stack[i], i);
    response.stackFrames.push_back(frame);
  }
  response.totalFrames = total_frames;
  return response;
}

void OnRequestStackTrace(
    DebugAdapterContext* ctx, const dap::StackTraceRequest& req,
    std::function<void(dap::ResponseOrError<dap::StackTraceResponse>)> callback) {
  Thread* thread = ctx->GetThread(static_cast<uint64_t>(req.threadId));
  if (thread) {
    if (thread->GetStack().has_all_frames()) {
      callback(PopulateStackTraceResponse(ctx, thread, req));
    } else {
      thread->GetStack().SyncFrames([ctx, weak_thread = thread->GetWeakPtr(),
                                     request = dap::StackTraceRequest(req),
                                     callback](const Err& err) {
        if (!err.has_error() && weak_thread) {
          callback(PopulateStackTraceResponse(ctx, weak_thread.get(), request));
        } else {
          callback(dap::Error("Thread exited, no frames."));
        }
      });
    }
  } else {
    callback(dap::Error("Thread not found."));
  }
}

}  // namespace zxdb
