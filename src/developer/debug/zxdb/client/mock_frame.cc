// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/mock_frame.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

MockFrame::MockFrame(Session* session, Thread* thread, const Location& location,
                     uint64_t sp, std::vector<Register> regs,
                     uint64_t frame_base, const Frame* physical_frame,
                     bool is_ambiguous_inline)
    : Frame(session),
      thread_(thread),
      sp_(sp),
      registers_(std::move(regs)),
      frame_base_(frame_base),
      physical_frame_(physical_frame),
      location_(location),
      is_ambiguous_inline_(is_ambiguous_inline) {}

MockFrame::~MockFrame() = default;

void MockFrame::SetAddress(uint64_t address) {
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
uint64_t MockFrame::GetAddress() const { return location_.address(); }
const std::vector<Register>& MockFrame::GetGeneralRegisters() const {
  return registers_;
}
std::optional<uint64_t> MockFrame::GetBasePointer() const {
  return frame_base_;
}
void MockFrame::GetBasePointerAsync(std::function<void(uint64_t)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [bp = frame_base_, cb]() { cb(bp); });
}
uint64_t MockFrame::GetStackPointer() const { return sp_; }

fxl::RefPtr<SymbolDataProvider> MockFrame::GetSymbolDataProvider() const {
  if (!symbol_data_provider_)
    symbol_data_provider_ = fxl::MakeRefCounted<MockSymbolDataProvider>();
  return symbol_data_provider_;
}

fxl::RefPtr<EvalContext> MockFrame::GetEvalContext() const {
  if (!eval_context_) {
    eval_context_ = fxl::MakeRefCounted<EvalContextImpl>(
        fxl::WeakPtr<const ProcessSymbols>(), GetSymbolDataProvider(),
        location_);
  }
  return eval_context_;
}

bool MockFrame::IsAmbiguousInlineLocation() const {
  return is_ambiguous_inline_;
}

}  // namespace zxdb
