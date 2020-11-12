// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/lib/applets_loader/applet.h"

namespace weavestack::applets {

Applet::~Applet() {
  if (is_valid()) {
    Delete();
  }
}

Applet::Applet(Applet&& o) noexcept
    : applets_handle_(o.applets_handle_), module_(std::move(o.module_)) {
  o.applets_handle_ = FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE;
}

Applet& Applet::operator=(Applet&& o) noexcept {
  applets_handle_ = o.applets_handle_;
  module_ = std::move(o.module_);
  o.applets_handle_ = FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE;
  return *this;
}

zx_status_t Applet::Create(FuchsiaWeaveAppletsCallbacksV1 callbacks) {
  FX_DCHECK(module_);
  FX_DCHECK(applets_handle_ == FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE);
  applets_handle_ = module_->create_applet(callbacks);
  return applets_handle_ != FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE ? ZX_OK : ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Applet::Delete() {
  FX_DCHECK(module_);
  FX_DCHECK(applets_handle_ != FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE);
  auto result = module_->delete_applet(applets_handle_) ? ZX_OK : ZX_ERR_NOT_SUPPORTED;
  module_ = nullptr;
  applets_handle_ = FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE;
  return result;
}

}  // namespace weavestack::applets
