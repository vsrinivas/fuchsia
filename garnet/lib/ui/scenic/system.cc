// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/system.h"

#include "garnet/lib/ui/scenic/scenic.h"

namespace scenic_impl {

SystemContext::SystemContext(sys::ComponentContext* app_context,
                             inspect::Object inspect_object,
                             fit::closure quit_callback)
    : app_context_(app_context),
      quit_callback_(std::move(quit_callback)),
      inspect_object_(std::move(inspect_object)) {
  FXL_DCHECK(app_context_);
}

SystemContext::SystemContext(SystemContext&& context)
    : SystemContext(context.app_context_, std::move(context.inspect_object_),
                    std::move(context.quit_callback_)) {
  auto& other_app_context =
      const_cast<sys::ComponentContext*&>(context.app_context_);
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

}  // namespace scenic_impl
