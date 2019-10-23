// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/client_eval_context_impl.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

// Do not store the Frame pointer because it may got out of scope before this class does.
ClientEvalContextImpl::ClientEvalContextImpl(const Frame* frame)
    : EvalContextImpl(frame->GetThread()->GetProcess()->GetSymbols()->GetWeakPtr(),
                      frame->GetSymbolDataProvider(), frame->GetLocation()),
      weak_target_(frame->GetThread()->GetProcess()->GetTarget()->GetWeakPtr()) {}

ClientEvalContextImpl::ClientEvalContextImpl(Target* target)
    : EvalContextImpl(target->GetProcess() ? target->GetProcess()->GetSymbols()->GetWeakPtr()
                                           : fxl::WeakPtr<const ProcessSymbols>(),
                      target->GetProcess() ? target->GetProcess()->GetSymbolDataProvider()
                                           : fxl::MakeRefCounted<SymbolDataProvider>(),
                      Location()),
      weak_target_(target->GetWeakPtr()) {}

VectorRegisterFormat ClientEvalContextImpl::GetVectorRegisterFormat() const {
  if (!weak_target_)
    return VectorRegisterFormat::kDouble;  // Reasonable default if the target is gone.

  std::string fmt = weak_target_->settings().GetString(ClientSettings::Target::kVectorFormat);

  if (fmt == ClientSettings::Target::kVectorFormat_i8)
    return VectorRegisterFormat::kSigned8;
  if (fmt == ClientSettings::Target::kVectorFormat_u8)
    return VectorRegisterFormat::kUnsigned8;

  if (fmt == ClientSettings::Target::kVectorFormat_i16)
    return VectorRegisterFormat::kSigned16;
  if (fmt == ClientSettings::Target::kVectorFormat_u16)
    return VectorRegisterFormat::kUnsigned16;

  if (fmt == ClientSettings::Target::kVectorFormat_i32)
    return VectorRegisterFormat::kSigned32;
  if (fmt == ClientSettings::Target::kVectorFormat_u32)
    return VectorRegisterFormat::kUnsigned32;

  if (fmt == ClientSettings::Target::kVectorFormat_i64)
    return VectorRegisterFormat::kSigned64;
  if (fmt == ClientSettings::Target::kVectorFormat_u64)
    return VectorRegisterFormat::kUnsigned64;

  if (fmt == ClientSettings::Target::kVectorFormat_i128)
    return VectorRegisterFormat::kSigned128;
  if (fmt == ClientSettings::Target::kVectorFormat_u128)
    return VectorRegisterFormat::kUnsigned128;

  if (fmt == ClientSettings::Target::kVectorFormat_float)
    return VectorRegisterFormat::kFloat;
  if (fmt == ClientSettings::Target::kVectorFormat_double)
    return VectorRegisterFormat::kDouble;

  // The settings schema should have validated the setting is one of the known ones.
  FXL_NOTREACHED();
  return VectorRegisterFormat::kDouble;
}

}  // namespace zxdb
