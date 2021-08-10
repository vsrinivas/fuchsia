// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/call_site_symbol_data_provider.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/register_info.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/expr/eval_dwarf_expr.h"
#include "src/developer/debug/zxdb/symbols/call_site.h"
#include "src/developer/debug/zxdb/symbols/call_site_parameter.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

CallSiteSymbolDataProvider::CallSiteSymbolDataProvider(
    fxl::WeakPtr<Process> process, const Location& return_location,
    fxl::RefPtr<SymbolDataProvider> frame_provider)
    : ProcessSymbolDataProvider(std::move(process)),
      call_site_symbol_context_(return_location.symbol_context()),
      frame_provider_(std::move(frame_provider)) {
  // Look up the call site definition (if any) associated with the return location.
  if (const CodeBlock* block = return_location.symbol().Get()->As<CodeBlock>()) {
    call_site_ =
        block->GetCallSiteForReturnTo(return_location.symbol_context(), return_location.address());
  }
}

CallSiteSymbolDataProvider::CallSiteSymbolDataProvider(
    fxl::WeakPtr<Process> process, fxl::RefPtr<CallSite> call_site,
    const SymbolContext& call_site_symbol_context, fxl::RefPtr<SymbolDataProvider> frame_provider)
    : ProcessSymbolDataProvider(std::move(process)),
      call_site_(std::move(call_site)),
      call_site_symbol_context_(call_site_symbol_context),
      frame_provider_(std::move(frame_provider)) {}

CallSiteSymbolDataProvider::~CallSiteSymbolDataProvider() = default;

fxl::RefPtr<SymbolDataProvider> CallSiteSymbolDataProvider::GetEntryDataProvider() const {
  return frame_provider_->GetEntryDataProvider();
}

std::optional<containers::array_view<uint8_t>> CallSiteSymbolDataProvider::GetRegister(
    debug::RegisterID id) {
  // The previous frame's data provider should have all the callee-saved registers. Any additional
  // registers provided by the CallSiteParameters can't always be evaluated synchronously, so we
  // don't try. Therefore, anything synchronous comes from the saved registers in the caller.
  fxl::RefPtr<CallSiteParameter> param = ParameterForRegister(id);
  if (param && !param->value_expr().empty())
    return std::nullopt;  // There is a parameter. Overrides need to be evaluated asynchronously.

  // No parameter, fall back to saved regular registers.
  if (IsRegisterCalleeSaved(id))
    return frame_provider_->GetRegister(id);

  // Anything else is synchronously known to be unknown.
  return containers::array_view<uint8_t>();
}

void CallSiteSymbolDataProvider::GetRegisterAsync(debug::RegisterID id, GetRegisterCallback cb) {
  fxl::RefPtr<CallSiteParameter> param = ParameterForRegister(id);
  if (!param || param->value_expr().empty()) {
    // No CallSiteParameter. If this is a caller-saved register, we can use the ones we have.
    if (IsRegisterCalleeSaved(id))
      return frame_provider_->GetRegisterAsync(id, std::move(cb));
    cb(Err("Call site register not available"), {});
    return;
  }

  // Callback that handles completion of the value_expr() evaluation.
  auto handle_done = [cb = std::move(cb)](DwarfExprEval& eval, const Err& err) mutable {
    if (err.has_error())
      return cb(err, {});
    if (eval.GetResultType() == DwarfExprEval::ResultType::kData)
      return cb(Err("DWARF expression produced unexpected results."), {});

    // The register value should be at the top of the stack. We could trim the stack entry to match
    // the byte width of the register, but this is expected to be used to prvide data back to the
    // DwarfExprEval which will pad it out again. So always pass all the bytes.
    std::vector<uint8_t> bytes(sizeof(DwarfExprEval::StackEntry));
    DwarfExprEval::StackEntry result = eval.GetResult();
    memcpy(bytes.data(), &result, sizeof(DwarfExprEval::StackEntry));

    cb(Err(), std::move(bytes));
  };

  // Dispatch the evaluation request. In practice, many call site expression evaluations will
  // complete synchronouslt because they're expressed in terms of other known registers. But the
  // contract for GetRegisterAsync is that it will always complete asynchronously. As a result,
  // always start execution from the message loop to prevent executing the callback from within
  // the caller's stack frame.
  //
  // Note that we pass the frame_provider_ as the symbol data provider instead of ourselves. Call
  // site parameters should not be expressed in terms of other call site parameters, so we only need
  // the underlying values. And this avoids the danger of infinitely recursive definitions.
  auto evaluator = fxl::MakeRefCounted<AsyncDwarfExprEval>(std::move(handle_done));
  debug::MessageLoop::Current()->PostTask(
      FROM_HERE,
      [evaluator, provider = frame_provider_, symbol_context = call_site_symbol_context_,
       expr = param->value_expr()]() { evaluator->Eval(provider, symbol_context, expr); });
}

void CallSiteSymbolDataProvider::WriteRegister(debug::RegisterID id, std::vector<uint8_t> data,
                                               WriteCallback cb) {
  // We don't support writing registers into previous stack frames.
  cb(Err("Writing registers is not supported in non-topmost stack frames."));
}

std::optional<uint64_t> CallSiteSymbolDataProvider::GetFrameBase() {
  return frame_provider_->GetFrameBase();
}

void CallSiteSymbolDataProvider::GetFrameBaseAsync(GetFrameBaseCallback callback) {
  return frame_provider_->GetFrameBaseAsync(std::move(callback));
}

uint64_t CallSiteSymbolDataProvider::GetCanonicalFrameAddress() const {
  return frame_provider_->GetCanonicalFrameAddress();
}

bool CallSiteSymbolDataProvider::IsRegisterCalleeSaved(debug::RegisterID id) {
  return process() && process()->session()->arch_info().abi()->IsRegisterCalleeSaved(id);
}

fxl::RefPtr<CallSiteParameter> CallSiteSymbolDataProvider::ParameterForRegister(
    debug::RegisterID id) {
  if (!call_site_)
    return nullptr;

  // Map to the DWARF register ID referenced by the call site parameters.
  const debug::RegisterInfo* info = debug::InfoForRegister(id);
  if (!info || info->dwarf_id == debug::RegisterInfo::kNoDwarfId)
    return nullptr;
  uint32_t dwarf_id = info->dwarf_id;

  // Brute-force search for a match (there are normally only a couple, and normally we only need
  // one value from a call site anyway).
  for (const auto& lazy_param : call_site_->parameters()) {
    const CallSiteParameter* param = lazy_param.Get()->As<CallSiteParameter>();
    if (param && param->location_register_num() && *param->location_register_num() == dwarf_id)
      return RefPtrTo(param);
  }

  return nullptr;
}

}  // namespace zxdb
