// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/public/lib/lifecycle/cpp/lifecycle_impl.h"

namespace modular {

LifecycleImpl::LifecycleImpl(app::ServiceNamespace* service_namespace,
                             LifecycleImpl::Delegate* delegate)
    : delegate_(delegate), binding_(this) {
  service_namespace->AddService<Lifecycle>(
      [this](fidl::InterfaceRequest<Lifecycle> request) {
        binding_.Bind(std::move(request));
      });
}

// |Lifecycle|
void LifecycleImpl::Terminate() {
  binding_.Close();
  delegate_->Terminate();
}

}  // namespace modular
