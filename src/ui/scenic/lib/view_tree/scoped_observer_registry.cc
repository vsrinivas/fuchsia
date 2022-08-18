// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scoped_observer_registry.h"

#include <lib/syslog/cpp/macros.h>

namespace view_tree {

ScopedRegistry::ScopedRegistry(std::shared_ptr<view_tree::GeometryProvider> geometry_provider)
    : geometry_provider_(std::move(geometry_provider)) {}

void ScopedRegistry::RegisterScopedViewTreeWatcher(
    zx_koid_t context_view,
    fidl::InterfaceRequest<fuchsia::ui::observation::geometry::ViewTreeWatcher> request,
    ScopedRegistry::RegisterScopedViewTreeWatcherCallback callback) {
  FX_DCHECK(geometry_provider_) << "GeometryProvider should be set up before this method call.";
  geometry_provider_->Register(std::move(request), context_view);

  callback();
}

void ScopedRegistry::Publish(sys::ComponentContext* app_context) {
  app_context->outgoing()->AddPublicService<fuchsia::ui::observation::scope::Registry>(
      bindings_.GetHandler(this));
}
}  // namespace view_tree
