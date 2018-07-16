// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/lifecycle/cpp/lifecycle_impl.h>

namespace modular {

LifecycleImpl::LifecycleImpl(component::ServiceNamespace* service_namespace,
                             LifecycleImpl::Delegate* delegate)
    : delegate_(delegate), binding_(this) {
  service_namespace->AddService<fuchsia::modular::Lifecycle>(
      [this](fidl::InterfaceRequest<fuchsia::modular::Lifecycle> request) {
        binding_.Bind(std::move(request));
      });
}

// |fuchsia::modular::Lifecycle|
void LifecycleImpl::Terminate() {
  binding_.Unbind();
  delegate_->Terminate();
}

}  // namespace modular
