// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/lib/applets_loader/applets_loader.h"

#include <dlfcn.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <optional>

namespace weavestack::applets {
namespace {

const FuchsiaWeaveAppletsModuleV1 kNullAppletModuleV1 = {
    .create_applet = nullptr,
    .delete_applet = nullptr,
};

}  // namespace

zx_status_t AppletsLoader::CreateWithModule(const char* lib_name,
                                            std::unique_ptr<AppletsLoader>* out) {
  auto module = AppletsModuleV1::Open(lib_name);
  if (!module) {
    return ZX_ERR_UNAVAILABLE;
  }
  *out = std::unique_ptr<AppletsLoader>(new AppletsLoader(std::move(module)));
  return ZX_OK;
}

std::unique_ptr<AppletsLoader> AppletsLoader::CreateWithNullModule() {
  // Note we use a no-op 'release' method here since we're wrapping a static/const data member we
  // want to make sure we don't free this pointer.
  auto module =
      std::shared_ptr<const FuchsiaWeaveAppletsModuleV1>(&kNullAppletModuleV1, [](auto* ptr) {});
  return std::unique_ptr<AppletsLoader>(new AppletsLoader(AppletsModuleV1(std::move(module))));
}

Applet AppletsLoader::CreateApplet(FuchsiaWeaveAppletsCallbacksV1 callbacks) {
  FX_DCHECK(module_);
  auto applets_handle = module_->create_applet(callbacks);
  if (applets_handle == FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE) {
    return {};
  }
  return {applets_handle, module_};
}

AppletsLoader::AppletsLoader(AppletsModuleV1 module) : module_(std::move(module)) {}

}  // namespace weavestack::applets
