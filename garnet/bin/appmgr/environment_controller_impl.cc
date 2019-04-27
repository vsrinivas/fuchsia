// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/environment_controller_impl.h"

#include <lib/async/default.h>
#include <lib/fit/function.h>

#include <utility>

#include "garnet/bin/appmgr/realm.h"

namespace component {

EnvironmentControllerImpl::EnvironmentControllerImpl(
    fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> request,
    std::unique_ptr<Realm> realm)
    : binding_(this),
      realm_(std::move(realm)),
      wait_(this, realm_->job().get(), ZX_TASK_TERMINATED) {
  if (request.is_valid()) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this](zx_status_t status) {
      realm_->parent()->ExtractChild(realm_.get());
      // The destructor of the temporary returned by ExtractChild destroys
      // |this| at the end of the previous statement.
    });
  }
  zx_status_t status = wait_.Begin(async_get_default_dispatcher());
  FXL_DCHECK(status == ZX_OK);
}

// Called when job terminates, regardless of if Kill() was invoked.
void EnvironmentControllerImpl::Handler(async_dispatcher_t* dispatcher,
                                        async::WaitBase* wait,
                                        zx_status_t status,
                                        const zx_packet_signal* signal) {
  FXL_DCHECK(status == ZX_OK);
  FXL_DCHECK((signal->observed & ZX_TASK_TERMINATED) == ZX_TASK_TERMINATED)
      << signal->observed;

  ExtractEnvironmentController();

  // The destructor of the temporary returned by ExtractComponent destroys
  // |this| at the end of the previous statement.
}

std::unique_ptr<EnvironmentControllerImpl>
EnvironmentControllerImpl::ExtractEnvironmentController() {
  if (realm_) {
    wait_.Cancel();
    auto self = realm_->parent()->ExtractChild(realm_.get());
    realm_ = nullptr;
    return self;
  }
  return nullptr;
}

EnvironmentControllerImpl::~EnvironmentControllerImpl() = default;

void EnvironmentControllerImpl::Kill(KillCallback callback) {
  auto self = ExtractEnvironmentController();
  callback();
  // The |self| destructor destroys |this| when we unwind this stack frame.
}

void EnvironmentControllerImpl::Detach() {
  binding_.set_error_handler(nullptr);
}

void EnvironmentControllerImpl::OnCreated() { binding_.events().OnCreated(); }

}  // namespace component
