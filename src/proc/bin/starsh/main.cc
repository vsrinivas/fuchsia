// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/starnix/developer/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/handle.h>
#include <lib/zx/socket.h>
#include <poll.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <memory>
#include <utility>

#include "src/lib/files/file_descriptor.h"
#include "src/lib/fsl/socket/socket_drainer.h"
#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/lib/line_input/line_input.h"

class Editor {
 public:
  explicit Editor(zx::socket sink) : fd_(STDIN_FILENO), sink_(std::move(sink)) {}

  void Start() {
    int flags = fcntl(fd_, F_GETFL, 0);
    ZX_ASSERT(flags >= 0);
    ZX_ASSERT(fcntl(fd_, F_SETFL, flags | O_NONBLOCK) >= 0);

    editor_ = std::make_unique<line_input::LineInputStdout>(
        [this](const std::string& line) { OnAccept(line); }, std::string());
    editor_->Show();
    WaitAsync();
  }

 private:
  void WaitAsync() {
    ZX_ASSERT(waiter_.Wait(
        [this](zx_status_t status, uint32_t events) {
          char buffer[1024];
          ssize_t actual = read(fd_, buffer, sizeof(buffer));
          ZX_ASSERT(actual >= 0);
          for (ssize_t i = 0; i < actual; ++i) {
            editor_->OnInput(buffer[i]);
          }
          WaitAsync();
        },
        fd_, POLLIN));
  }

  void OnAccept(const std::string& line) {
    std::string line_with_newline = line + "\n";
    fsl::BlockingCopyFromString(line_with_newline, sink_);
  }

  int fd_;
  zx::socket sink_;
  fsl::FDWaiter waiter_;
  std::unique_ptr<line_input::LineInputStdout> editor_;
};

class ForwardToFd : public fsl::SocketDrainer::Client {
 public:
  explicit ForwardToFd(int fd) : fd_(fd) {}
  virtual ~ForwardToFd() = default;

  void OnDataAvailable(const void* data, size_t num_bytes) final {
    ZX_ASSERT(fxl::WriteFileDescriptor(fd_, static_cast<const char*>(data), num_bytes));
  }

  void OnDataComplete() final {}

 private:
  int fd_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto services = sys::ServiceDirectory::CreateFromNamespace();

  fuchsia::starnix::developer::ManagerSyncPtr manager;
  services->Connect(manager.NewRequest());

  zx_status_t status = ZX_OK;
  zx::socket local_in, remote_in;
  zx::socket local_out, remote_out;
  zx::socket local_err, remote_err;

  status = zx::socket::create(ZX_SOCKET_STREAM, &local_in, &remote_in);
  ZX_ASSERT_MSG(status == ZX_OK, "status = %d (%s)", status, zx_status_get_string(status));

  status = zx::socket::create(ZX_SOCKET_STREAM, &local_out, &remote_out);
  ZX_ASSERT_MSG(status == ZX_OK, "status = %d (%s)", status, zx_status_get_string(status));

  status = zx::socket::create(ZX_SOCKET_STREAM, &local_err, &remote_err);
  ZX_ASSERT_MSG(status == ZX_OK, "status = %d (%s)", status, zx_status_get_string(status));

  Editor editor(std::move(local_in));
  editor.Start();

  ForwardToFd forward_out(STDOUT_FILENO);
  fsl::SocketDrainer drain_out(&forward_out);
  drain_out.Start(std::move(local_out));

  ForwardToFd forward_err(STDERR_FILENO);
  fsl::SocketDrainer drain_err(&forward_err);
  drain_err.Start(std::move(local_err));

  auto params = fuchsia::starnix::developer::ShellParams();
  params.set_standard_in(std::move(remote_in));
  params.set_standard_out(std::move(remote_out));
  params.set_standard_err(std::move(remote_err));

  fuchsia::starnix::developer::ShellControllerPtr controller;
  manager->StartShell(std::move(params), controller.NewRequest());

  controller.set_error_handler([&loop](zx_status_t status) {
    fprintf(stderr, "[starnix shell exited with %d (%s)]\n", status, zx_status_get_string(status));
    loop.Quit();
  });

  loop.Run();
  return 0;
}
