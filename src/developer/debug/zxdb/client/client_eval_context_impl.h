// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CLIENT_EVAL_CONTEXT_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CLIENT_EVAL_CONTEXT_IMPL_H_

#include "src/developer/debug/zxdb/client/setting_store_observer.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Frame;
class System;
class Target;

// Provides client-specific integration for EvalContextImpl.
class ClientEvalContextImpl : public EvalContextImpl, public SettingStoreObserver {
 public:
  // The input pointers are not saved so the caller should not need to worry about lifetime (this
  // class is refcounted and may outlive a frame or target).
  //
  // Neither the Frame nor the Target variants accept a null pointer, but the Target variant does
  // not need to have a currently running process.
  explicit ClientEvalContextImpl(const Frame* frame, std::optional<ExprLanguage> language);
  explicit ClientEvalContextImpl(Target* target, std::optional<ExprLanguage> language);

  ~ClientEvalContextImpl() override;

  // EvalContext overrides.
  VectorRegisterFormat GetVectorRegisterFormat() const override;
  bool ShouldPromoteToDerived() const override { return auto_cast_to_derived_; }

 private:
  // SettingStoreObserver implementation:
  void OnSettingChanged(const SettingStore&, const std::string& setting_name) override;

  void RefreshAutoCastToDerived();

  fxl::WeakPtr<Target> weak_target_;
  fxl::WeakPtr<System> weak_system_;

  // Cached value for the setting. Since this is requested frequently and almost never changed, we
  // don't want to do a setting lookup by string every time. Instead this class watches the
  // setting value to update this cache as needed by RefreshAutoCastToDerived().
  bool auto_cast_to_derived_ = false;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CLIENT_EVAL_CONTEXT_IMPL_H_
