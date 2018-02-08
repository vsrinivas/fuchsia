// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/cpp/wait.h>
#include <async/default.h>
#include <fdio/limits.h>
#include <fdio/util.h>
#include <poll.h>
#include <stdio.h>
#include <iostream>

#include "garnet/lib/machina/fidl/serial.fidl.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/app/fidl/application_controller.fidl.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/fsl/socket/socket_drainer.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/svc/cpp/services.h"

// Reads output from a socket provided by the guest and writes the data to
// stdout. This data generally comes from emulated serial devices (ex:
// virtio-console).
class OutputWriter : public fsl::SocketDrainer::Client {
 public:
  OutputWriter() : socket_drainer_(this) {}

  void Start(zx::socket socket) { socket_drainer_.Start(std::move(socket)); }

  // |fsl::SocketDrainer::Client|
  void OnDataAvailable(const void* data, size_t num_bytes) override {
    std::cout.write(static_cast<const char*>(data), num_bytes);
    std::cout.flush();
  }

  void OnDataComplete() override {}

 private:
  fsl::SocketDrainer socket_drainer_;
};

// Reads bytes from stdin and writes them to a socket provided by the guest.
// These bytes are generally delivered to emulated serial devices (ex:
// virtio-console).
class InputReader {
 public:
  InputReader() : async_(async_get_default()) {}

  void Start(zx_handle_t socket) {
    socket_ = socket;
    WaitForKeystroke();
  }

 private:
  void WaitForKeystroke() {
    if (!std::cin.eof()) {
      fd_waiter_.Wait(fbl::BindMember(this, &InputReader::HandleKeystroke),
                      STDIN_FILENO, POLLIN);
    }
  }

  void HandleKeystroke(zx_status_t status, uint32_t events) {
    if (status != ZX_OK) {
      return;
    }
    int res = std::cin.get();
    if (res < 0) {
      return;
    }

    // Treat backspace as DEL to play nicely with terminal emulation.
    pending_key_ = res == '\b' ? 0x7f : static_cast<char>(res);
    SendKeyToGuest();
  }

  void SendKeyToGuest() {
    async_wait_result_t result = OnSocketReady(async_, ZX_OK, nullptr);
    if (result == ASYNC_WAIT_AGAIN) {
      wait_.set_object(socket_);
      wait_.set_trigger(ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED |
                        ZX_SOCKET_PEER_CLOSED);
      wait_.set_handler(fbl::BindMember(this, &InputReader::OnSocketReady));
      wait_.Begin(async_);
    }
  }

  async_wait_result_t OnSocketReady(async_t* async,
                                    zx_status_t status,
                                    const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      return ASYNC_WAIT_FINISHED;
    }
    status = zx_socket_write(socket_, 0, &pending_key_, 1, nullptr);
    if (status == ZX_ERR_SHOULD_WAIT) {
      return ASYNC_WAIT_AGAIN;
    }
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error " << status << " writing to socket";
      return ASYNC_WAIT_FINISHED;
    }
    pending_key_ = 0;
    WaitForKeystroke();
    return ASYNC_WAIT_FINISHED;
  }

  zx_handle_t socket_ = ZX_HANDLE_INVALID;
  fsl::FDWaiter fd_waiter_;
  char pending_key_;
  async_t* async_;
  async::Wait wait_;
};

static app::FileDescriptorPtr CloneFileDescriptor(int fd) {
  zx_handle_t handles[FDIO_MAX_HANDLES] = {0, 0, 0};
  uint32_t types[FDIO_MAX_HANDLES] = {
      ZX_HANDLE_INVALID,
      ZX_HANDLE_INVALID,
      ZX_HANDLE_INVALID,
  };
  zx_status_t status = fdio_clone_fd(fd, 0, handles, types);
  if (status <= 0) {
    return nullptr;
  }
  app::FileDescriptorPtr result = app::FileDescriptor::New();
  result->type0 = types[0];
  result->handle0 = zx::handle(handles[0]);
  result->type1 = types[1];
  result->handle1 = zx::handle(handles[1]);
  result->type2 = types[2];
  result->handle2 = zx::handle(handles[2]);
  return result;
}

int main(int argc, const char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: launch_guest <guest_program> <args>*\n");
    return 1;
  }

  fsl::MessageLoop loop;
  // Watch stdin for new input.
  InputReader input_reader;
  // Write socket output to stdout.
  OutputWriter output_writer;

  // Setup launch request.
  app::Services services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = argv[1];
  for (int i = 0; i < argc - 2; ++i) {
    launch_info->arguments.push_back(argv[2 + i]);
  }
  launch_info->service_request = services.NewRequest();
  launch_info->out = CloneFileDescriptor(STDOUT_FILENO);
  launch_info->err = CloneFileDescriptor(STDERR_FILENO);

  // Connect to app launcher & create guest.
  app::ApplicationLauncherSyncPtr launcher;
  app::ConnectToEnvironmentService(GetSynchronousProxy(&launcher));
  app::ApplicationControllerPtr controller;
  launcher->CreateApplication(std::move(launch_info), controller.NewRequest());
  controller.set_error_handler([&loop] {
    FXL_LOG(INFO) << "Launched application terminated.";
    loop.PostQuitTask();
  });

  // Open the serial service of the guest and process IO.
  fidl::InterfacePtr<machina::SerialService> serial;
  services.ConnectToService(serial.NewRequest());
  serial->Connect([&output_writer, &input_reader](zx::socket socket) {
    input_reader.Start(socket.get());
    output_writer.Start(std::move(socket));
  });

  loop.Run();
  return 0;
}
