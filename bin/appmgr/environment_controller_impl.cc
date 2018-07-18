// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/environment_controller_impl.h"

#include <utility>

#include <lib/fit/function.h>

#include "garnet/bin/appmgr/realm.h"

namespace component {

EnvironmentControllerImpl::EnvironmentControllerImpl(
    fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> request,
    std::unique_ptr<Realm> realm)
    : binding_(this), realm_(std::move(realm)) {
  if (request.is_valid()) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this] {
      realm_->parent()->ExtractChild(realm_.get());
      // The destructor of the temporary returned by ExtractChild destroys
      // |this| at the end of the previous statement.
    });
  }
}

EnvironmentControllerImpl::~EnvironmentControllerImpl() = default;

void EnvironmentControllerImpl::Kill(KillCallback callback) {
  std::unique_ptr<EnvironmentControllerImpl> self =
      realm_->parent()->ExtractChild(realm_.get());
  realm_ = nullptr;
  callback();
  // The |self| destructor destroys |this| when we unwind this stack frame.
}

void EnvironmentControllerImpl::Detach() {
  binding_.set_error_handler(nullptr);
}

void EnvironmentControllerImpl::OnCreated() {
  binding_.events().OnCreated();
}

}  // namespace component
