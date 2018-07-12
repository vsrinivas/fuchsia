// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/serial.h"

#include <poll.h>
#include <iostream>

#include <lib/fdio/util.h>
#include <fuchsia/guest/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>

#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/socket/socket_drainer.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/logging.h"

// Reads bytes from stdin and writes them to a socket provided by the guest.
// These bytes are generally delivered to emulated serial devices (ex:
// virtio-console).
class InputReader {
 public:
  InputReader() : dispatcher_(async_get_default_dispatcher()) {}

  void Start(zx_handle_t socket) {
    socket_ = socket;
    wait_.set_object(socket_);
    wait_.set_trigger(ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED |
                      ZX_SOCKET_PEER_CLOSED);
    WaitForKeystroke();
  }

 private:
  void WaitForKeystroke() {
    if (!std::cin.eof()) {
      fd_waiter_.Wait(fit::bind_member(this, &InputReader::HandleKeystroke),
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

  void SendKeyToGuest() { OnSocketReady(dispatcher_, &wait_, ZX_OK, nullptr); }

  void OnSocketReady(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      return;
    }
    status = zx_socket_write(socket_, 0, &pending_key_, 1, nullptr);
    if (status == ZX_ERR_SHOULD_WAIT) {
      wait->Begin(dispatcher);  // ignore errors
      return;
    }
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error " << status << " writing to socket";
      return;
    }
    pending_key_ = 0;
    WaitForKeystroke();
  }

  zx_handle_t socket_ = ZX_HANDLE_INVALID;
  fsl::FDWaiter fd_waiter_;
  char pending_key_;
  async_dispatcher_t* dispatcher_;
  async::WaitMethod<InputReader, &InputReader::OnSocketReady> wait_{this};
};

// Reads output from a socket provided by the guest and writes the data to
// stdout. This data generally comes from emulated serial devices (ex:
// virtio-console).
class OutputWriter : public fsl::SocketDrainer::Client {
 public:
  OutputWriter(async::Loop* loop) : loop_(loop), socket_drainer_(this) {}

  void Start(zx::socket socket) { socket_drainer_.Start(std::move(socket)); }

  // |fsl::SocketDrainer::Client|
  void OnDataAvailable(const void* data, size_t num_bytes) override {
    std::cout.write(static_cast<const char*>(data), num_bytes);
    std::cout.flush();
  }

  void OnDataComplete() override { loop_->Shutdown(); }

 private:
  async::Loop* loop_;
  fsl::SocketDrainer socket_drainer_;
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
  input_reader_->Start(socket.get());
  output_writer_->Start(std::move(socket));
}

void handle_serial(uint32_t env_id, uint32_t cid) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  // Connect to environment.
  fuchsia::guest::GuestManagerSync2Ptr guestmgr;
  fuchsia::sys::ConnectToEnvironmentService(guestmgr.NewRequest());
  fuchsia::guest::GuestEnvironmentSync2Ptr env_ptr;
  guestmgr->ConnectToEnvironment(env_id, env_ptr.NewRequest());

  fuchsia::guest::GuestControllerSync2Ptr guest_controller;
  env_ptr->ConnectToGuest(cid, guest_controller.NewRequest());

  // Open the serial service of the guest and process IO.
  zx::socket socket;
  guest_controller->GetSerial(&socket);
  if (!socket) {
    std::cerr << "Failed to open serial port\n";
    return;
  }
  SerialConsole console(&loop);
  console.Start(std::move(socket));
  loop.Run();
}
