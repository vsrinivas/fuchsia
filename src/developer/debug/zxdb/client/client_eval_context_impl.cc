// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/client_eval_context_impl.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

// Do not store the Frame pointer because it may got out of scope before this class does.
ClientEvalContextImpl::ClientEvalContextImpl(const Frame* frame,
                                             std::optional<ExprLanguage> language)
    : EvalContextImpl(frame->GetThread()->GetProcess()->GetSymbols()->GetWeakPtr(),
                      frame->GetSymbolDataProvider(), frame->GetLocation(), language),
      weak_target_(frame->GetThread()->GetProcess()->GetTarget()->GetWeakPtr()),
      weak_system_(frame->session()->system().GetWeakPtr()) {
  frame->session()->system().settings().AddObserver(ClientSettings::System::kAutoCastToDerived,
                                                    this);
  RefreshAutoCastToDerived();
}

ClientEvalContextImpl::ClientEvalContextImpl(Target* target, std::optional<ExprLanguage> language)
    : EvalContextImpl(target->GetProcess() ? target->GetProcess()->GetSymbols()->GetWeakPtr()
                                           : fxl::WeakPtr<const ProcessSymbols>(),
                      target->GetProcess() ? target->GetProcess()->GetSymbolDataProvider()
                                           : fxl::MakeRefCounted<SymbolDataProvider>(),
                      Location(), language),
      weak_target_(target->GetWeakPtr()),
      weak_system_(target->session()->system().GetWeakPtr()) {
  target->session()->system().settings().AddObserver(ClientSettings::System::kAutoCastToDerived,
                                                     this);
  RefreshAutoCastToDerived();
}

ClientEvalContextImpl::~ClientEvalContextImpl() {
  if (weak_system_)
    weak_system_->settings().RemoveObserver(ClientSettings::System::kAutoCastToDerived, this);
}

VectorRegisterFormat ClientEvalContextImpl::GetVectorRegisterFormat() const {
  if (!weak_target_)
    return VectorRegisterFormat::kDouble;  // Reasonable default if the target is gone.

  std::string fmt = weak_target_->settings().GetString(ClientSettings::Target::kVectorFormat);
  if (auto found = StringToVectorRegisterFormat(fmt))
    return *found;

  // The settings schema should have validated the setting is one of the known ones.
  FXL_NOTREACHED();
  return VectorRegisterFormat::kDouble;
}

void ClientEvalContextImpl::OnSettingChanged(const SettingStore&, const std::string& setting_name) {
  if (setting_name == ClientSettings::System::kAutoCastToDerived)
    RefreshAutoCastToDerived();
}

void ClientEvalContextImpl::RefreshAutoCastToDerived() {
  if (weak_system_) {
    auto_cast_to_derived_ =
        weak_system_->settings().GetBool(ClientSettings::System::kAutoCastToDerived);
  }
}

}  // namespace zxdb
