// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/module/cpp/module_impl.h"

namespace modular {

ModuleImpl::ModuleImpl(component::ServiceNamespace* service_namespace,
                       Delegate* delegate)
    : delegate_(delegate), binding_(this) {
  service_namespace->AddService<Module>(
      [this](f1dl::InterfaceRequest<Module> request) {
        binding_.Bind(std::move(request));
      });
}

// |Module|
void ModuleImpl::Initialize(
    f1dl::InterfaceHandle<ModuleContext> module_context,
    f1dl::InterfaceRequest<component::ServiceProvider> outgoing_services) {
  delegate_->ModuleInit(std::move(module_context),
                        std::move(outgoing_services));
}

}  // namespace modular
