// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "display_manager_impl.h"
#include "src/lib/fxl/logging.h"

namespace display {

DisplayManagerImpl::DisplayManagerImpl()
    : DisplayManagerImpl(sys::ComponentContext::Create()) {}

DisplayManagerImpl::DisplayManagerImpl(
    std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {
  display_ = std::unique_ptr<Display>(Display::GetDisplay());

  context_->outgoing()->AddPublicService<Manager>(
      [this](fidl::InterfaceRequest<Manager> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void DisplayManagerImpl::GetBrightness(GetBrightnessCallback callback) {
  if (NULL == display_.get()) {
    FXL_LOG(ERROR) << "GetBrightness: display not retrieved";
    callback(false, 0.0f);
    return;
  }

  double brightness;
  callback(display_->GetBrightness(&brightness), brightness);
}

void DisplayManagerImpl::SetBrightness(double brightness,
                                       SetBrightnessCallback callback) {
  if (NULL == display_.get()) {
    FXL_LOG(ERROR) << "SetBrightness: display not retrieved";
    callback(false);
    return;
  }

  callback(display_->SetBrightness(brightness));
}
}  // namespace display
