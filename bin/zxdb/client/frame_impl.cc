// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/frame_impl.h"

#include "garnet/bin/zxdb/client/frame_symbol_data_provider.h"
#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/thread_impl.h"
#include "garnet/bin/zxdb/expr/symbol_eval_context.h"

namespace zxdb {

FrameImpl::FrameImpl(ThreadImpl* thread,
                     const debug_ipc::StackFrame& stack_frame,
                     Location location)
    : Frame(thread->session()),
      thread_(thread),
      stack_frame_(stack_frame),
      location_(std::move(location)) {}

FrameImpl::~FrameImpl() {
  if (symbol_data_provider_)
    symbol_data_provider_->DisownFrame();
}

Thread* FrameImpl::GetThread() const { return thread_; }

const Location& FrameImpl::GetLocation() const {
  EnsureSymbolized();
  return location_;
}

uint64_t FrameImpl::GetAddress() const { return location_.address(); }

uint64_t FrameImpl::GetStackPointer() const { return stack_frame_.sp; }

void FrameImpl::EnsureSymbolized() const {
  if (location_.is_symbolized())
    return;
  location_ =
      thread_->process()->GetSymbols()->LocationForAddress(location_.address());
}

fxl::RefPtr<SymbolDataProvider> FrameImpl::GetSymbolDataProvider() const {
  if (!symbol_data_provider_) {
    symbol_data_provider_ = fxl::MakeRefCounted<FrameSymbolDataProvider>(
        const_cast<FrameImpl*>(this));
  }
  return symbol_data_provider_;
}

fxl::RefPtr<ExprEvalContext> FrameImpl::GetExprEvalContext() const {
  if (!symbol_eval_context_) {
    EnsureSymbolized();
    symbol_eval_context_ = fxl::MakeRefCounted<SymbolEvalContext>(
        GetSymbolDataProvider(), location_);
  }
  return symbol_eval_context_;
}

}  // namespace zxdb
