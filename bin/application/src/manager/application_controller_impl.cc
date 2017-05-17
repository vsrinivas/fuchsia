// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/src/manager/application_controller_impl.h"

#include <utility>

#include "application/src/manager/application_environment_impl.h"
#include "lib/ftl/functional/closure.h"
#include "lib/mtl/tasks/message_loop.h"

namespace app {

ApplicationControllerImpl::ApplicationControllerImpl(
    fidl::InterfaceRequest<ApplicationController> request,
    ApplicationEnvironmentImpl* environment,
    std::unique_ptr<archive::FileSystem> fs,
    mx::process process,
    std::string path)
    : binding_(this),
      environment_(environment),
      fs_(std::move(fs)),
      process_(std::move(process)),
      path_(std::move(path)) {
  termination_handler_ = mtl::MessageLoop::GetCurrent()->AddHandler(
      this, process_.get(), MX_TASK_TERMINATED);
  if (request.is_pending()) {
    binding_.Bind(std::move(request));
    binding_.set_connection_error_handler([this] { Kill(); });
  }
}

ApplicationControllerImpl::~ApplicationControllerImpl() {
  mtl::MessageLoop::GetCurrent()->RemoveHandler(termination_handler_);
  // Two ways we end up here:
  // 1) OnHandleReady() destroys this object; in which case, process is dead.
  // 2) Our owner destroys this object; in which case, the process may still be
  //    alive.
  if (process_)
    process_.kill();
}

void ApplicationControllerImpl::Kill() {
  process_.kill();
}

void ApplicationControllerImpl::Detach() {
  binding_.set_connection_error_handler(ftl::Closure());
}

// Called when process terminates, regardless of if Kill() was invoked.
void ApplicationControllerImpl::OnHandleReady(mx_handle_t handle,
                                              mx_signals_t pending) {
  FTL_DCHECK(handle == process_.get());
  FTL_DCHECK(pending & MX_TASK_TERMINATED);

  process_.reset();

  environment_->ExtractApplication(this);
  // The destructor of the temporary returned by ExtractApplication destroys
  // |this| at the end of the previous statement.
}

}  // namespace app
