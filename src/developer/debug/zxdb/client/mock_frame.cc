// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/mock_frame.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/expr/symbol_eval_context.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

MockFrame::MockFrame(Session* session, Thread* thread,
                     const debug_ipc::StackFrame& stack_frame,
                     const Location& location, uint64_t frame_base,
                     const Frame* physical_frame, bool is_ambiguous_inline)
    : Frame(session),
      thread_(thread),
      stack_frame_(stack_frame),
      frame_base_(frame_base),
      physical_frame_(physical_frame),
      location_(location),
      is_ambiguous_inline_(is_ambiguous_inline) {}
MockFrame::~MockFrame() = default;

void MockFrame::SetAddress(uint64_t address) {
  stack_frame_.ip = address;
  location_ = Location(address, location_.file_line(), location_.column(),
                       location_.symbol_context(), location_.symbol());
}

void MockFrame::SetFileLine(const FileLine& file_line) {
  location_ = Location(location_.address(), file_line, location_.column(),
                       location_.symbol_context(), location_.symbol());
}

Thread* MockFrame::GetThread() const { return thread_; }

bool MockFrame::IsInline() const { return !!physical_frame_; }

const Frame* MockFrame::GetPhysicalFrame() const {
  if (physical_frame_)
    return physical_frame_;
  return this;
}

const Location& MockFrame::GetLocation() const { return location_; }
uint64_t MockFrame::GetAddress() const { return stack_frame_.ip; }
const std::vector<debug_ipc::Register>& MockFrame::GetGeneralRegisters() const {
  return stack_frame_.regs;
}
std::optional<uint64_t> MockFrame::GetBasePointer() const {
  return frame_base_;
}
void MockFrame::GetBasePointerAsync(std::function<void(uint64_t)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [bp = frame_base_, cb]() { cb(bp); });
}
uint64_t MockFrame::GetStackPointer() const { return stack_frame_.sp; }

fxl::RefPtr<SymbolDataProvider> MockFrame::GetSymbolDataProvider() const {
  if (!symbol_data_provider_)
    symbol_data_provider_ = fxl::MakeRefCounted<MockSymbolDataProvider>();
  return symbol_data_provider_;
}

fxl::RefPtr<ExprEvalContext> MockFrame::GetExprEvalContext() const {
  if (!symbol_eval_context_) {
    symbol_eval_context_ = fxl::MakeRefCounted<SymbolEvalContext>(
        fxl::WeakPtr<const ProcessSymbols>(), GetSymbolDataProvider(),
        location_);
  }
  return symbol_eval_context_;
}

bool MockFrame::IsAmbiguousInlineLocation() const {
  return is_ambiguous_inline_;
}

}  // namespace zxdb
