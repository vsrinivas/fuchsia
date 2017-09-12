// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/moterm/command.h"

#include <magenta/status.h>
#include <unistd.h>

#include "lib/fxl/logging.h"

namespace moterm {
namespace {

mx_status_t AddRedirectedSocket(
    std::vector<fsl::StartupHandle>* startup_handles,
    int startup_fd,
    mx::socket* out_socket) {
  fsl::StartupHandle startup_handle;
  mx_status_t status =
      fsl::CreateRedirectedSocket(startup_fd, out_socket, &startup_handle);
  if (status != MX_OK)
    return status;
  startup_handles->push_back(std::move(startup_handle));
  return MX_OK;
}

std::vector<const char*> GetArgv(const std::vector<std::string>& command) {
  std::vector<const char*> argv;
  argv.reserve(command.size());
  for (const auto& arg : command)
    argv.push_back(arg.c_str());
  return argv;
}

}  // namespace

Command::Command() = default;

Command::~Command() {
  if (termination_key_)
    fsl::MessageLoop::GetCurrent()->RemoveHandler(termination_key_);
  if (out_key_)
    fsl::MessageLoop::GetCurrent()->RemoveHandler(out_key_);
  if (err_key_)
    fsl::MessageLoop::GetCurrent()->RemoveHandler(err_key_);
}

bool Command::Start(std::vector<std::string> command,
                    std::vector<fsl::StartupHandle> startup_handles,
                    ReceiveCallback receive_callback,
                    fxl::Closure termination_callback) {
  FXL_DCHECK(!command.empty());

  mx_status_t status;
  if ((status = AddRedirectedSocket(&startup_handles, STDIN_FILENO, &stdin_))) {
    FXL_LOG(ERROR) << "Failed to create stdin pipe: status=" << status;
    return false;
  }
  if ((status =
           AddRedirectedSocket(&startup_handles, STDOUT_FILENO, &stdout_))) {
    FXL_LOG(ERROR) << "Failed to create stdout pipe: status=" << status;
    return false;
  }
  if ((status =
           AddRedirectedSocket(&startup_handles, STDERR_FILENO, &stderr_))) {
    FXL_LOG(ERROR) << "Failed to create stderr pipe: status=" << status;
    return false;
  }

  std::vector<uint32_t> ids;
  std::vector<mx_handle_t> handles;
  for (const auto& startup_handle : startup_handles) {
    ids.push_back(startup_handle.id);
    handles.push_back(startup_handle.handle.get());
  }

  launchpad_t* lp;
  launchpad_create(0, command[0].c_str(), &lp);
  launchpad_clone(lp, LP_CLONE_ALL & ~LP_CLONE_MXIO_STDIO);
  launchpad_set_args(lp, command.size(), GetArgv(command).data());
  launchpad_add_handles(lp, ids.size(), handles.data(), ids.data());
  launchpad_load_from_file(lp, command[0].c_str());

  mx_handle_t proc;
  const char* errmsg;
  status = launchpad_go(lp, &proc, &errmsg);
  if (status != MX_OK) {
    FXL_LOG(ERROR) << "Cannot run executable " << command[0] << " due to error "
                   << status << " (" << mx_status_get_string(status)
                   << "): " << errmsg;
    return false;
  }
  process_.reset(proc);

  termination_callback_ = std::move(termination_callback);
  receive_callback_ = std::move(receive_callback);

  termination_key_ = fsl::MessageLoop::GetCurrent()->AddHandler(
      this, process_.get(), MX_PROCESS_TERMINATED);

  out_key_ = fsl::MessageLoop::GetCurrent()->AddHandler(this, stdout_.get(),
                                                        MX_SOCKET_READABLE);
  err_key_ = fsl::MessageLoop::GetCurrent()->AddHandler(this, stderr_.get(),
                                                        MX_SOCKET_READABLE);
  return true;
}

void Command::OnHandleReady(mx_handle_t handle,
                            mx_signals_t pending,
                            uint64_t count) {
  if (handle == process_.get() && (pending & MX_PROCESS_TERMINATED)) {
    fsl::MessageLoop::GetCurrent()->RemoveHandler(termination_key_);
    termination_key_ = 0;
    termination_callback_();
  } else if (pending & MX_SOCKET_READABLE) {
    char buffer[2048];
    size_t len;

    if (handle == stdout_.get()) {
      if (stdout_.read(0, buffer, sizeof(buffer), &len) != MX_OK) {
        return;
      }
    } else if (handle == stderr_.get()) {
      if (stderr_.read(0, buffer, sizeof(buffer), &len) != MX_OK) {
        return;
      }
    } else {
      return;
    }
    receive_callback_(buffer, len);
  }
}

void Command::SendData(const void* bytes, size_t num_bytes) {
  size_t len;
  if (stdin_.write(0, bytes, num_bytes, &len) != MX_OK) {
    // TODO: Deal with the socket being full.
    FXL_LOG(ERROR) << "Failed to send";
  }
}

}  // namespace moterm
