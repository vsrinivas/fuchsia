// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "observer_registry.h"

#include <lib/syslog/cpp/log_level.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

namespace view_tree {

Registry::Registry(std::shared_ptr<view_tree::GeometryProviderManager> geometry_provider_manager)
    : geometry_provider_manager_(std::move(geometry_provider_manager)) {}

void Registry::RegisterGlobalGeometryProvider(
    fidl::InterfaceRequest<fuchsia::ui::observation::geometry::Provider> request,
    Registry::RegisterGlobalGeometryProviderCallback callback) {
  FX_DCHECK(geometry_provider_manager_)
      << "GeometryProviderManager should be set up before this method call.";
  geometry_provider_manager_->RegisterGlobalGeometryProvider(std::move(request));

  callback();
}

void Registry::Publish(sys::ComponentContext* app_context) {
  app_context->outgoing()->AddPublicService<fuchsia::ui::observation::test::Registry>(
      bindings_.GetHandler(this));
}
}  // namespace view_tree
