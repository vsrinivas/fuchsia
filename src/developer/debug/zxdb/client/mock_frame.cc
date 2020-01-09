// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/mock_frame.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

namespace {

Location MakeLocation(TargetPointer ip, const std::string& func_name, FileLine file_line) {
  // The function name currently can't handle "::". Because we pass the string to set_assigned_name,
  // they will be treated as literals and not scope separators. If support for those is needed,
  // we need to make the hierarchy of namespaces, etc. to put the function in.
  FXL_DCHECK(func_name.find("::") == std::string::npos);

  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name(func_name);

  return Location(ip, std::move(file_line), 0, SymbolContext::ForRelativeAddresses(), function);
}

}  // namespace

MockFrame::MockFrame(Session* session, Thread* thread, const Location& location, uint64_t sp,
                     uint64_t cfa, std::vector<debug_ipc::Register> regs, uint64_t frame_base,
                     const Frame* physical_frame, bool is_ambiguous_inline)
    : Frame(session),
      thread_(thread),
      sp_(sp),
      cfa_(cfa),
      general_registers_(std::move(regs)),
      frame_base_(frame_base),
      physical_frame_(physical_frame),
      location_(location),
      is_ambiguous_inline_(is_ambiguous_inline) {}

MockFrame::MockFrame(Session* session, Thread* thread, TargetPointer ip, TargetPointer sp,
                     const std::string& func_name, FileLine file_line)
    : MockFrame(session, thread, MakeLocation(ip, func_name, std::move(file_line)), sp) {}

MockFrame::~MockFrame() = default;

void MockFrame::SetAddress(uint64_t address) {
  location_ = Location(address, location_.file_line(), location_.column(),
                       location_.symbol_context(), location_.symbol());
}

void MockFrame::SetFileLine(const FileLine& file_line) {
  location_ = Location(location_.address(), file_line, location_.column(),
                       location_.symbol_context(), location_.symbol());
}

MockSymbolDataProvider* MockFrame::GetMockSymbolDataProvider() {
  GetSymbolDataProvider();  // Force creation.
  return symbol_data_provider_.get();
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

const std::vector<debug_ipc::Register>* MockFrame::GetRegisterCategorySync(
    debug_ipc::RegisterCategory category) const {
  if (category == debug_ipc::RegisterCategory::kGeneral)
    return &general_registers_;
  return nullptr;
}

void MockFrame::GetRegisterCategoryAsync(
    debug_ipc::RegisterCategory category,
    fit::function<void(const Err&, const std::vector<debug_ipc::Register>&)> cb) {
  Err err;
  std::vector<debug_ipc::Register> regs;
  if (category == debug_ipc::RegisterCategory::kGeneral)
    regs = general_registers_;
  else
    err = Err("Register category unavailable from mock.");

  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [err, regs, cb = std::move(cb)]() mutable { cb(err, regs); });
}

void MockFrame::WriteRegister(debug_ipc::RegisterID id, std::vector<uint8_t> data,
                              fit::callback<void(const Err&)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
    cb(Err("Writing registers not (yet) supported by the mock."));
  });
}

std::optional<uint64_t> MockFrame::GetBasePointer() const { return frame_base_; }

void MockFrame::GetBasePointerAsync(fit::callback<void(uint64_t)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [bp = frame_base_, cb = std::move(cb)]() mutable { cb(bp); });
}

uint64_t MockFrame::GetStackPointer() const { return sp_; }

uint64_t MockFrame::GetCanonicalFrameAddress() const { return cfa_; }

fxl::RefPtr<SymbolDataProvider> MockFrame::GetSymbolDataProvider() const {
  if (!symbol_data_provider_)
    symbol_data_provider_ = fxl::MakeRefCounted<MockSymbolDataProvider>();
  return symbol_data_provider_;
}

fxl::RefPtr<EvalContext> MockFrame::GetEvalContext() const {
  if (!eval_context_) {
    eval_context_ = fxl::MakeRefCounted<EvalContextImpl>(fxl::WeakPtr<const ProcessSymbols>(),
                                                         GetSymbolDataProvider(), location_);
  }
  return eval_context_;
}

bool MockFrame::IsAmbiguousInlineLocation() const { return is_ambiguous_inline_; }

}  // namespace zxdb
