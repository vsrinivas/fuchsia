// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/system.h"

#include "garnet/lib/ui/scenic/scenic.h"

namespace scenic {

SystemContext::SystemContext(component::StartupContext* app_context,
                             fit::closure quit_callback)
    : app_context_(app_context), quit_callback_(std::move(quit_callback)) {
  FXL_DCHECK(app_context_);
}

SystemContext::SystemContext(SystemContext&& context)
    : SystemContext(context.app_context_, std::move(context.quit_callback_)) {
  auto& other_app_context =
      const_cast<component::StartupContext*&>(context.app_context_);
  other_app_context = nullptr;
}

System::System(SystemContext context, bool initialized_after_construction)
    : initialized_(initialized_after_construction),
      context_(std::move(context)) {}

void System::SetToInitialized() {
  initialized_ = true;
  if (on_initialized_callback_) {
    on_initialized_callback_(this);
    on_initialized_callback_ = nullptr;
  }
}

System::~System() = default;

TempSystemDelegate::TempSystemDelegate(SystemContext context,
                                       bool initialized_after_construction)
    : System(std::move(context), initialized_after_construction) {}

}  // namespace scenic
