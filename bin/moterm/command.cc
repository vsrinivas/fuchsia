// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/moterm/command.h"

#include <unistd.h>
#include <zircon/status.h>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

namespace moterm {
namespace {

zx_status_t AddRedirectedSocket(
    std::vector<fsl::StartupHandle>* startup_handles,
    int startup_fd,
    zx::socket* out_socket) {
  fsl::StartupHandle startup_handle;
  zx_status_t status =
      fsl::CreateRedirectedSocket(startup_fd, out_socket, &startup_handle);
  if (status != ZX_OK)
    return status;
  startup_handles->push_back(std::move(startup_handle));
  return ZX_OK;
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

bool Command::Start(std::vector<std::string> command,
                    std::vector<fsl::StartupHandle> startup_handles,
                    ReceiveCallback receive_callback,
                    fxl::Closure termination_callback) {
  FXL_DCHECK(!command.empty());

  zx_status_t status;
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
  std::vector<zx_handle_t> handles;
  for (const auto& startup_handle : startup_handles) {
    ids.push_back(startup_handle.id);
    handles.push_back(startup_handle.handle.get());
  }

  launchpad_t* lp;
  launchpad_create(0, command[0].c_str(), &lp);
  launchpad_clone(lp, LP_CLONE_ALL & ~LP_CLONE_FDIO_STDIO);
  launchpad_set_args(lp, command.size(), GetArgv(command).data());
  launchpad_add_handles(lp, ids.size(), handles.data(), ids.data());
  launchpad_load_from_file(lp, command[0].c_str());

  zx_handle_t proc;
  const char* errmsg;
  status = launchpad_go(lp, &proc, &errmsg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot run executable " << command[0] << " due to error "
                   << status << " (" << zx_status_get_string(status)
                   << "): " << errmsg;
    return false;
  }
  process_.reset(proc);

  termination_callback_ = std::move(termination_callback);
  receive_callback_ = std::move(receive_callback);

  termination_waiter_ =
      std::make_unique<async::AutoWait>(fsl::MessageLoop::GetCurrent()->async(),
                                        process_.get(), ZX_PROCESS_TERMINATED);
  termination_waiter_->set_handler(std::bind(
      &Command::OnProcessTerminated, this, process_.get(),
      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  termination_waiter_->Begin();

  stdout_waiter_ =
      std::make_unique<async::AutoWait>(fsl::MessageLoop::GetCurrent()->async(),
                                        stdout_.get(), ZX_SOCKET_READABLE);
  stdout_waiter_->set_handler(std::bind(
      &Command::OnSocketReadable, this, &stdout_, std::placeholders::_1,
      std::placeholders::_2, std::placeholders::_3));
  stdout_waiter_->Begin();

  stderr_waiter_ =
      std::make_unique<async::AutoWait>(fsl::MessageLoop::GetCurrent()->async(),
                                        stderr_.get(), ZX_SOCKET_READABLE);
  stderr_waiter_->set_handler(std::bind(
      &Command::OnSocketReadable, this, &stderr_, std::placeholders::_1,
      std::placeholders::_2, std::placeholders::_3));
  stderr_waiter_->Begin();

  return true;
}

async_wait_result_t Command::OnProcessTerminated(
    zx_handle_t process_handle,
    async_t*,
    zx_status_t status,
    const zx_packet_signal* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Command::OnProcessTerminated received an error status code: "
        << status;
    return ASYNC_WAIT_FINISHED;
  }
  zx_signals_t pending = signal->observed;
  FXL_DCHECK(pending & ZX_PROCESS_TERMINATED);
  FXL_DCHECK(process_handle == process_.get());
  termination_callback_();
  termination_waiter_.reset();
  return ASYNC_WAIT_FINISHED;
}

async_wait_result_t Command::OnSocketReadable(zx::socket* socket,
                                              async_t*,
                                              zx_status_t status,
                                              const zx_packet_signal* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Command::OnSocketReadable received an error status code: "
        << status;
    return ASYNC_WAIT_FINISHED;
  }
  zx_signals_t pending = signal->observed;
  FXL_DCHECK(pending & ZX_SOCKET_READABLE);
  char buffer[2048];
  size_t len;

  if (socket->read(0, buffer, sizeof(buffer), &len) != ZX_OK) {
    FXL_LOG(ERROR) << "Command::OnSocketReadable error reading from socket.";
    return ASYNC_WAIT_FINISHED;
  }
  receive_callback_(buffer, len);
  return ASYNC_WAIT_AGAIN;
}

void Command::SendData(const void* bytes, size_t num_bytes) {
  size_t len;
  if (stdin_.write(0, bytes, num_bytes, &len) != ZX_OK) {
    // TODO: Deal with the socket being full.
    FXL_LOG(ERROR) << "Failed to send";
  }
}

}  // namespace moterm
