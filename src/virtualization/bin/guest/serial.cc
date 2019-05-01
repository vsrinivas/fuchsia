// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest/serial.h"

#include <fcntl.h>
#include <poll.h>

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/fit/function.h>

#include "lib/fsl/socket/socket_drainer.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "src/lib/fxl/logging.h"

// Reads bytes from stdin and writes them to a socket provided by the guest.
// These bytes are generally delivered to emulated serial devices (ex:
// virtio-console).
class InputReader {
 public:
  void Start(zx::unowned_socket socket) {
    socket_ = std::move(socket);
    wait_.set_object(socket_->get());
    wait_.set_trigger(ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED |
                      ZX_SOCKET_PEER_CLOSED);
    WaitForKeystroke();
  }

 private:
  void WaitForKeystroke() {
    if (fcntl(STDIN_FILENO, F_GETFD) != -1) {
      fd_waiter_.Wait(fit::bind_member(this, &InputReader::HandleKeystroke),
                      STDIN_FILENO, POLLIN);
    }
  }

  void SendKeyToGuest() {
    zx_status_t status = socket_->write(0, &pending_key_, 1, nullptr);
    if (status == ZX_ERR_SHOULD_WAIT) {
      wait_.Begin(async_get_default_dispatcher());  // ignore errors
      return;
    } else if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error " << status << " writing to socket";
      return;
    }
    WaitForKeystroke();
  }

  void HandleKeystroke(zx_status_t status, uint32_t events) {
    if (status != ZX_OK) {
      return;
    }
    ssize_t actual = read(STDIN_FILENO, &pending_key_, 1);
    if (actual != 1) {
      return;
    }
    switch (pending_key_) {
      case '\b':
        pending_key_ = 0x7f;
        break;
      case '\r':
        pending_key_ = '\n';
        break;
    }
    SendKeyToGuest();
  }

  void OnSocketReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                     zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      return;
    }
    SendKeyToGuest();
  }

  zx::unowned_socket socket_;
  fsl::FDWaiter fd_waiter_;
  char pending_key_;
  async::WaitMethod<InputReader, &InputReader::OnSocketReady> wait_{this};
};

// Reads output from a socket provided by the guest and writes the data to
// stdout. This data generally comes from emulated serial devices (ex:
// virtio-console).
class OutputWriter : public fsl::SocketDrainer::Client {
 public:
  OutputWriter(async::Loop* loop) : loop_(loop) {}

  void Start(zx::socket socket) { socket_drainer_.Start(std::move(socket)); }

  // |fsl::SocketDrainer::Client|
  void OnDataAvailable(const void* data, size_t num_bytes) override {
    write(STDOUT_FILENO, data, num_bytes);
  }

  void OnDataComplete() override { loop_->Shutdown(); }

 private:
  async::Loop* loop_;
  fsl::SocketDrainer socket_drainer_{this};
};

SerialConsole::SerialConsole(async::Loop* loop)
    : loop_(loop),
      input_reader_(std::make_unique<InputReader>()),
      output_writer_(std::make_unique<OutputWriter>(loop)) {}

SerialConsole::SerialConsole(SerialConsole&& o)
    : loop_(o.loop_),
      input_reader_(std::move(o.input_reader_)),
      output_writer_(std::move(o.output_writer_)) {}

SerialConsole::~SerialConsole() = default;

void SerialConsole::Start(zx::socket socket) {
  input_reader_->Start(zx::unowned_socket(socket));
  output_writer_->Start(std::move(socket));
}

void handle_serial(uint32_t env_id, uint32_t cid, async::Loop* loop,
                   sys::ComponentContext* context) {
  // Connect to environment.
  fuchsia::guest::EnvironmentManagerSyncPtr environment_manager;
  context->svc()->Connect(environment_manager.NewRequest());
  fuchsia::guest::EnvironmentControllerSyncPtr env_ptr;
  environment_manager->Connect(env_id, env_ptr.NewRequest());

  fuchsia::guest::InstanceControllerSyncPtr instance_controller;
  env_ptr->ConnectToInstance(cid, instance_controller.NewRequest());

  // Open the serial service of the guest and process IO.
  zx::socket socket;
  instance_controller->GetSerial(&socket);
  if (!socket) {
    FXL_LOG(ERROR) << "Failed to open serial port";
    return;
  }
  SerialConsole console(loop);
  console.Start(std::move(socket));
  loop->Run();
}
