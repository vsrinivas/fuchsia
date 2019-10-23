// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/frame_impl.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/client_eval_context_impl.h"
#include "src/developer/debug/zxdb/client/frame_symbol_data_provider.h"
#include "src/developer/debug/zxdb/client/process_impl.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread_impl.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/developer/debug/zxdb/symbols/variable_location.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

FrameImpl::FrameImpl(Thread* thread, const debug_ipc::StackFrame& stack_frame, Location location)
    : Frame(thread->session()),
      thread_(thread),
      sp_(stack_frame.sp),
      cfa_(stack_frame.cfa),
      location_(std::move(location)),
      weak_factory_(this) {
  registers_[static_cast<size_t>(debug_ipc::RegisterCategory::kGeneral)] = stack_frame.regs;
}

FrameImpl::~FrameImpl() {
  if (symbol_data_provider_)
    symbol_data_provider_->Disown();
}

Thread* FrameImpl::GetThread() const { return thread_; }

bool FrameImpl::IsInline() const { return false; }

const Frame* FrameImpl::GetPhysicalFrame() const { return this; }

const Location& FrameImpl::GetLocation() const {
  EnsureSymbolized();
  return location_;
}

uint64_t FrameImpl::GetAddress() const { return location_.address(); }

const std::vector<debug_ipc::Register>* FrameImpl::GetRegisterCategorySync(
    debug_ipc::RegisterCategory category) const {
  FXL_DCHECK(category <= debug_ipc::RegisterCategory::kLast);

  size_t category_index = static_cast<size_t>(category);
  FXL_DCHECK(category_index < static_cast<size_t>(debug_ipc::RegisterCategory::kLast));

  if (registers_[category_index])
    return &*registers_[category_index];
  return nullptr;
}

void FrameImpl::GetRegisterCategoryAsync(
    debug_ipc::RegisterCategory category,
    fit::function<void(const Err&, const std::vector<debug_ipc::Register>&)> cb) {
  FXL_DCHECK(category < debug_ipc::RegisterCategory::kLast &&
             category != debug_ipc::RegisterCategory::kNone);

  size_t category_index = static_cast<size_t>(category);
  FXL_DCHECK(category_index < static_cast<size_t>(debug_ipc::RegisterCategory::kLast));

  if (registers_[category_index]) {
    // Registers known already, asynchronously return the result.
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb), weak_frame = weak_factory_.GetWeakPtr(), category_index]() {
          if (weak_frame)
            cb(Err(), *weak_frame->registers_[category_index]);
          else
            cb(Err("Frame destroyed before registers could be retrieved."), {});
        });
    return;
  }

  debug_ipc::ReadRegistersRequest request;
  request.process_koid = thread_->GetProcess()->GetKoid();
  request.thread_koid = thread_->GetKoid();
  request.categories.push_back(category);

  session()->remote_api()->ReadRegisters(
      request, [weak_frame = weak_factory_.GetWeakPtr(), category, cb = std::move(cb)](
                   const Err& err, debug_ipc::ReadRegistersReply reply) mutable {
        if (!weak_frame)
          return cb(Err("Frame destroyed before registers could be retrieved."), {});

        weak_frame->registers_[static_cast<size_t>(category)] = reply.registers;
        cb(Err(), std::move(reply.registers));
      });
  return;
}

std::optional<uint64_t> FrameImpl::GetBasePointer() const {
  // This function is logically const even though EnsureBasePointer does some
  // potentially mutating things underneath (calling callbacks and such).
  if (const_cast<FrameImpl*>(this)->EnsureBasePointer()) {
    FXL_DCHECK(computed_base_pointer_);
    return computed_base_pointer_;
  }
  return std::nullopt;
}

void FrameImpl::GetBasePointerAsync(fit::callback<void(uint64_t bp)> cb) {
  if (EnsureBasePointer()) {
    // BP available synchronously but we don't want to reenter the caller.
    FXL_DCHECK(computed_base_pointer_);
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [bp = *computed_base_pointer_, cb = std::move(cb)]() mutable { cb(bp); });
  } else {
    // Add pending request for when evaluation is complete.
    FXL_DCHECK(base_pointer_eval_ && !base_pointer_eval_->is_complete());
    base_pointer_requests_.push_back(std::move(cb));
  }
}

uint64_t FrameImpl::GetStackPointer() const { return sp_; }

uint64_t FrameImpl::GetCanonicalFrameAddress() const { return cfa_; }

void FrameImpl::EnsureSymbolized() const {
  if (location_.is_symbolized())
    return;
  auto vect =
      thread_->GetProcess()->GetSymbols()->ResolveInputLocation(InputLocation(location_.address()));
  // Should always return 1 result for symbolizing addresses.
  FXL_DCHECK(vect.size() == 1);
  location_ = std::move(vect[0]);
}

fxl::RefPtr<SymbolDataProvider> FrameImpl::GetSymbolDataProvider() const {
  if (!symbol_data_provider_) {
    symbol_data_provider_ =
        fxl::MakeRefCounted<FrameSymbolDataProvider>(const_cast<FrameImpl*>(this));
  }
  return symbol_data_provider_;
}

fxl::RefPtr<EvalContext> FrameImpl::GetEvalContext() const {
  if (!symbol_eval_context_)
    symbol_eval_context_ = fxl::MakeRefCounted<ClientEvalContextImpl>(this);
  return symbol_eval_context_;
}

bool FrameImpl::IsAmbiguousInlineLocation() const {
  // This object always represents physical frames which aren't ambiguous.
  return false;
}

bool FrameImpl::EnsureBasePointer() {
  if (computed_base_pointer_)
    return true;  // Already have it available synchronously.

  if (base_pointer_eval_) {
    // Already happening asynchronously.
    FXL_DCHECK(!base_pointer_eval_->is_complete());
    return false;
  }

  const Location& loc = GetLocation();
  if (!loc.symbol()) {
    // Unsymbolized.
    computed_base_pointer_ = 0;
    return true;
  }

  const Function* function = loc.symbol().Get()->AsFunction();
  const VariableLocation::Entry* location_entry = nullptr;
  if (!function ||
      !(location_entry = function->frame_base().EntryForIP(loc.symbol_context(), GetAddress()))) {
    // No frame base declared for this function.
    computed_base_pointer_ = 0;
    return true;
  }

  // Try to evaluate the location.
  base_pointer_eval_ = std::make_unique<DwarfExprEval>();

  // Callback when the expression is done. Will normally get called reentrantly
  // by DwarfExpreval::Eval().
  //
  // Binding |this| here is OK because the DwarfExprEval is owned by us and
  // won't give callbacks after it's destroyed.
  auto save_result = [this](DwarfExprEval* eval, const Err&) {
    if (eval->is_success()) {
      computed_base_pointer_ = eval->GetResult();
    } else {
      // We don't currently report errors for frame base requests, but instead
      // just fall back on what was computed by the backend.
      computed_base_pointer_ = 0;
    }

    // Issue callbacks for everybody waiting. Moving to a local here prevents
    // weirdness if a callback calls back into us, and also clears the vector.
    std::vector<fit::callback<void(uint64_t)>> callbacks = std::move(base_pointer_requests_);
    for (auto& cb : callbacks)
      cb(*computed_base_pointer_);
  };

  auto eval_result = base_pointer_eval_->Eval(GetSymbolDataProvider(), loc.symbol_context(),
                                              location_entry->expression, std::move(save_result));

  // In the common case this will complete synchronously and the above callback
  // will have put the result into base_pointer_requests_ before this code is
  // executed.
  return eval_result == DwarfExprEval::Completion::kSync;
}

}  // namespace zxdb
