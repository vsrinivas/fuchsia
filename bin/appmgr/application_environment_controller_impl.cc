// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/application_environment_controller_impl.h"

#include <utility>

#include "garnet/bin/appmgr/job_holder.h"
#include "lib/fxl/functional/closure.h"

namespace app {

ApplicationEnvironmentControllerImpl::ApplicationEnvironmentControllerImpl(
    fidl::InterfaceRequest<ApplicationEnvironmentController> request,
    std::unique_ptr<JobHolder> job_holder)
    : binding_(this), job_holder_(std::move(job_holder)) {
  if (request.is_pending()) {
    binding_.Bind(std::move(request));
    binding_.set_connection_error_handler([this] {
      job_holder_->parent()->ExtractChild(job_holder_.get());
      // The destructor of the temporary returned by ExtractChild destroys
      // |this| at the end of the previous statement.
    });
  }
}

ApplicationEnvironmentControllerImpl::~ApplicationEnvironmentControllerImpl() =
    default;

void ApplicationEnvironmentControllerImpl::Kill(const KillCallback& callback) {
  std::unique_ptr<ApplicationEnvironmentControllerImpl> self =
      job_holder_->parent()->ExtractChild(job_holder_.get());
  job_holder_ = nullptr;
  callback();
  // The |self| destructor destroys |this| when we unwind this stack frame.
}

void ApplicationEnvironmentControllerImpl::Detach() {
  binding_.set_connection_error_handler(fxl::Closure());
}

}  // namespace app
