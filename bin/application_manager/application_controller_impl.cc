// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/application_manager/application_controller_impl.h"

#include <utility>

#include "apps/modular/src/application_manager/application_environment_impl.h"
#include "lib/ftl/functional/closure.h"

namespace modular {

ApplicationControllerImpl::ApplicationControllerImpl(
    fidl::InterfaceRequest<ApplicationController> request,
    ApplicationEnvironmentImpl* environment,
    mx::process process)
    : binding_(this), environment_(environment), process_(std::move(process)) {
  if (request.is_pending()) {
    binding_.Bind(std::move(request));
    binding_.set_connection_error_handler([this] {
      environment_->ExtractApplication(this);
      // The destructor of the temporary returned by ExtractApplication destroys
      // |this| at the end of the previous statement.
    });
  }
}

ApplicationControllerImpl::~ApplicationControllerImpl() = default;

void ApplicationControllerImpl::Kill(const KillCallback& callback) {
  std::unique_ptr<ApplicationControllerImpl> self =
      environment_->ExtractApplication(this);
  process_.kill();
  process_.reset();
  callback();
  // The |self| destructor destroys |this| when we unwind this stack frame.
}

void ApplicationControllerImpl::Detach() {
  binding_.set_connection_error_handler(ftl::Closure());
}

}  // namespace modular
