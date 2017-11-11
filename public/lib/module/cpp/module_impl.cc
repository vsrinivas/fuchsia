// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/module/cpp/module_impl.h"

namespace modular {

ModuleImpl::ModuleImpl(app::ServiceNamespace* service_namespace,
                       Delegate* delegate)
    : delegate_(delegate), binding_(this) {
  service_namespace->AddService<Module>(
      [this](fidl::InterfaceRequest<Module> request) {
        binding_.Bind(std::move(request));
      });
}

// |Module|
void ModuleImpl::Initialize(
    fidl::InterfaceHandle<ModuleContext> module_context,
    fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) {
  delegate_->ModuleInit(std::move(module_context),
                        std::move(outgoing_services));
}

}  // namespace modular
