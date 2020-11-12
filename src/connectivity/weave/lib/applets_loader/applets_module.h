// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_APPLETS_MODULE_H_
#define SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_APPLETS_MODULE_H_

#include <memory>

#include "src/connectivity/weave/lib/applets/weave_applets.h"

namespace weavestack::applets {

namespace internal {

template <typename ModuleImpl>
class AppletsModule {
 public:
  static AppletsModule<ModuleImpl> Open(const char* name);

  AppletsModule() = default;
  explicit AppletsModule(std::shared_ptr<const ModuleImpl> module);

  // Provide access to the underlying module structure.
  const ModuleImpl& operator*() const { return *module_; }
  const ModuleImpl* operator->() const { return module_.get(); }

  [[nodiscard]] bool is_valid() const { return static_cast<bool>(module_); }
  explicit operator bool() const { return is_valid(); }

  // Releases the reference to the module. After a call to |Release| the |AppletsModule| will be in
  // an invalid state (that is |is_valid| will return false).
  void Release() { module_ = nullptr; }
  AppletsModule& operator=(std::nullptr_t) {
    Release();
    return *this;
  }

 private:
  std::shared_ptr<const ModuleImpl> module_;
};

}  // namespace internal

using AppletsModuleV1 = internal::AppletsModule<FuchsiaWeaveAppletsModuleV1>;

}  // namespace weavestack::applets

#endif  // SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_APPLETS_MODULE_H_
