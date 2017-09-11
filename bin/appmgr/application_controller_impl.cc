// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/application_controller_impl.h"

#include <utility>

#include "garnet/bin/appmgr/application_environment_impl.h"
#include "lib/fxl/functional/closure.h"
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
  binding_.set_connection_error_handler(fxl::Closure());
}

bool ApplicationControllerImpl::SendReturnCodeIfTerminated() {
  // Get process info.
  mx_info_process_t process_info;
  mx_status_t result = process_.get_info(MX_INFO_PROCESS, &process_info,
                                         sizeof(process_info), NULL, NULL);
  FXL_DCHECK(result == MX_OK);

  if (process_info.exited) {
    // If the process has exited, call the callbacks.
    for (const auto& iter : wait_callbacks_) {
      iter(process_info.return_code);
    }
    wait_callbacks_.clear();
  }

  return process_info.exited;
}

void ApplicationControllerImpl::Wait(const WaitCallback& callback) {
  wait_callbacks_.push_back(callback);
  SendReturnCodeIfTerminated();
}

// Called when process terminates, regardless of if Kill() was invoked.
void ApplicationControllerImpl::OnHandleReady(mx_handle_t handle,
                                              mx_signals_t pending,
                                              uint64_t count) {
  FXL_DCHECK(handle == process_.get());
  FXL_DCHECK(pending & MX_TASK_TERMINATED);

  if (!wait_callbacks_.empty()) {
    bool terminated = SendReturnCodeIfTerminated();
    FXL_DCHECK(terminated);
  }

  process_.reset();

  environment_->ExtractApplication(this);
  // The destructor of the temporary returned by ExtractApplication destroys
  // |this| at the end of the previous statement.
}

}  // namespace app
