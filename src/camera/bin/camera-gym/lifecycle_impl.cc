// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/camera-gym/lifecycle_impl.h"

#include <lib/async-loop/default.h>

LifecycleImpl::LifecycleImpl(fit::closure on_terminate)
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), on_terminate_(std::move(on_terminate)) {
  loop_.StartThread("Lifecycle Thread");
}

LifecycleImpl::~LifecycleImpl() { loop_.Shutdown(); }

fidl::InterfaceRequestHandler<fuchsia::modular::Lifecycle> LifecycleImpl::GetHandler() {
  return fit::bind_member(this, &LifecycleImpl::OnNewRequest);
}

void LifecycleImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::modular::Lifecycle> request) {
  bindings_.AddBinding(this, std::move(request), loop_.dispatcher());
}

void LifecycleImpl::Terminate() { on_terminate_(); }
