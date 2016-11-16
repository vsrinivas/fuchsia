// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/moterm/command.h"

#include "lib/ftl/logging.h"
#include "lib/mtl/io/redirection.h"

namespace moterm {
namespace {

mx_status_t AddRedirectedSocket(modular::ApplicationLaunchInfo* launch_info,
                                int startup_fd,
                                mx::socket* out_socket) {
  mtl::StartupHandle startup_handle;
  mx_status_t status =
      mtl::CreateRedirectedSocket(startup_fd, out_socket, &startup_handle);
  if (status != NO_ERROR)
    return status;
  launch_info->startup_handles.insert(startup_handle.id,
                                      std::move(startup_handle.handle));
  return NO_ERROR;
}

}  // namespace

Command::Command() = default;

Command::~Command() {
  if (out_key_) {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(out_key_);
  }
  if (err_key_) {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(err_key_);
  }
}

bool Command::Start(modular::ApplicationLauncher* launcher,
                    std::vector<std::string> command,
                    ReceiveCallback receive_callback,
                    ftl::Closure termination_callback) {
  FTL_DCHECK(!command.empty());

  auto launch_info = modular::ApplicationLaunchInfo::New();
  launch_info->url = command[0];
  for (size_t i = 1; i < command.size(); ++i)
    launch_info->arguments.push_back(command[i]);

  mx_status_t status;
  if ((status =
           AddRedirectedSocket(launch_info.get(), STDIN_FILENO, &stdin_))) {
    FTL_LOG(ERROR) << "Failed to create stdin pipe: status=" << status;
    return false;
  }
  if ((status =
           AddRedirectedSocket(launch_info.get(), STDOUT_FILENO, &stdout_))) {
    FTL_LOG(ERROR) << "Failed to create stdout pipe: status=" << status;
    return false;
  }
  if ((status =
           AddRedirectedSocket(launch_info.get(), STDERR_FILENO, &stderr_))) {
    FTL_LOG(ERROR) << "Failed to create stderr pipe: status=" << status;
    return false;
  }

  FTL_LOG(INFO) << "Starting " << command[0];
  launcher->CreateApplication(std::move(launch_info),
                              GetProxy(&application_controller_));
  application_controller_.set_connection_error_handler(
      std::move(termination_callback));

  receive_callback_ = std::move(receive_callback);
  out_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(this, stdout_.get(),
                                                        MX_SOCKET_READABLE);
  err_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(this, stderr_.get(),
                                                        MX_SOCKET_READABLE);
  return true;
}

void Command::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  if (pending & MX_SOCKET_READABLE) {
    char buffer[2048];
    mx_size_t len;

    if (handle == stdout_.get()) {
      if (stdout_.read(0, buffer, sizeof(buffer), &len) != NO_ERROR) {
        return;
      }
    } else if (handle == stderr_.get()) {
      if (stderr_.read(0, buffer, sizeof(buffer), &len) != NO_ERROR) {
        return;
      }
    } else {
      return;
    }
    receive_callback_(buffer, len);
  }
}

void Command::SendData(const void* bytes, size_t num_bytes) {
  mx_size_t len;
  if (stdin_.write(0, bytes, num_bytes, &len) != NO_ERROR) {
    // TODO: Deal with the socket being full.
    FTL_LOG(ERROR) << "Failed to send";
  }
}

}  // namespace moterm
