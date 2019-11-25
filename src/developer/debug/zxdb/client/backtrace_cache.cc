// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/backtrace_cache.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"

using namespace debug_ipc;

namespace zxdb {

namespace {

void AppendFrame(const Location& location, const TargetSymbols* symbols, Backtrace* backtrace) {
  if (!location.is_valid()) {
    backtrace->frames.push_back({});
    return;
  }

  Backtrace::Frame frame;
  if (!location.has_symbols()) {
    frame.address = location.address();
    backtrace->frames.push_back(std::move(frame));
    return;
  }

  frame.file_line = location.file_line();

  const Function* function = location.symbol().Get()->AsFunction();
  if (!function) {
    backtrace->frames.push_back(std::move(frame));
    return;
  }

  frame.function_name = function->GetFullName();
  backtrace->frames.push_back(std::move(frame));
}

}  // namespace

BacktraceCache::BacktraceCache() : weak_factory_(this) {}
BacktraceCache::~BacktraceCache() = default;

fxl::WeakPtr<BacktraceCache> BacktraceCache::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void BacktraceCache::OnThreadStopped(Thread* thread, debug_ipc::ExceptionType type,
                                     const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (!should_cache_ || type != ExceptionType::kSoftware)
    return;

  if (thread->GetState() != ThreadRecord::State::kBlocked)
    return;

  auto& stack = thread->GetStack();
  if (stack.has_all_frames()) {
    StoreBacktrace(stack);
    return;
  }

  // If the stack is not complete, we attempt to get it. In most cases, this requirement should be
  // posted before a resume call (we're in the middle of a thread exception notification), so we
  // should get the frames reliably for normal cases.
  stack.SyncFrames([stack = stack.GetWeakPtr(), cache = GetWeakPtr()](const Err& err) {
    if (err.has_error() || !stack)
      return;

    cache->StoreBacktrace(*stack);
  });
}

void BacktraceCache::StoreBacktrace(const Stack& stack) {
  Backtrace backtrace;
  for (size_t i = 0; i < stack.size(); i++) {
    const Frame* frame = stack[i];
    Thread* thread = frame->GetThread();

    // Tests can provide a null thread for a frame.
    const TargetSymbols* syms = !thread ? nullptr : thread->GetProcess()->GetTarget()->GetSymbols();
    AppendFrame(frame->GetLocation(), syms, &backtrace);
  }

  backtraces_.push_back(std::move(backtrace));
}

}  // namespace zxdb
