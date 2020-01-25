// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/system.h"

namespace scenic_impl {

SystemContext::SystemContext(sys::ComponentContext* app_context,
                             inspect_deprecated::Node inspect_node, fit::closure quit_callback)
    : app_context_(app_context),
      quit_callback_(std::move(quit_callback)),
      inspect_node_(std::move(inspect_node)) {
  FXL_DCHECK(app_context_);
}

SystemContext::SystemContext(SystemContext&& context)
    : SystemContext(context.app_context_, std::move(context.inspect_node_),
                    std::move(context.quit_callback_)) {
  auto& other_app_context = const_cast<sys::ComponentContext*&>(context.app_context_);
  other_app_context = nullptr;
}

System::System(SystemContext context) : context_(std::move(context)) {}

System::~System() = default;

}  // namespace scenic_impl
