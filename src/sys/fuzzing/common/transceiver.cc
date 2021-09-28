// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/transceiver.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/status.h>

namespace fuzzing {

struct Transceiver::Request {
  enum State : uint8_t {
    kStop,
    kReceive,
    kTransmit,
  } type;

  FidlInput rx_input;
  ReceiveCallback rx_callback;

  Input tx_input;
  zx::socket tx_sender;

  Request() { type = kStop; }

  Request(FidlInput input, ReceiveCallback callback) {
    type = kReceive;
    rx_input = std::move(input);
    rx_callback = std::move(callback);
  }

  Request(Input input, zx::socket sender) {
    type = kTransmit;
    tx_input = std::move(input);
    tx_sender = std::move(sender);
  }
};

Transceiver::Transceiver() {
  worker_ = std::thread([this]() { Worker(); });
}

Transceiver::~Transceiver() {
  Pend(std::make_unique<Request>());
  sync_completion_signal(&sync_);
  if (worker_.joinable()) {
    worker_.join();
  }
}

void Transceiver::Pend(std::unique_ptr<Request> request) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.push_back(std::move(request));
    sync_completion_signal(&sync_);
  }
}

void Transceiver::Worker() {
  while (true) {
    std::unique_ptr<Request> request;
    // Wait indefinitely. Destroying this object will send |kStop|.
    sync_completion_wait(&sync_, ZX_TIME_INFINITE);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      request = std::move(requests_.front());
      requests_.pop_front();
      if (requests_.empty()) {
        sync_completion_reset(&sync_);
      }
    }
    switch (request->type) {
      case Request::kStop:
        return;
      case Request::kReceive:
        ReceiveImpl(std::move(request->rx_input), std::move(request->rx_callback));
        break;
      case Request::kTransmit:
        TransmitImpl(request->tx_input, std::move(request->tx_sender));
        break;
      default:
        FX_NOTREACHED();
    }
  }
}

void Transceiver::Receive(FidlInput input, ReceiveCallback callback) {
  Pend(std::make_unique<Request>(std::move(input), std::move(callback)));
}

void Transceiver::ReceiveImpl(FidlInput&& fidl_input, Transceiver::ReceiveCallback callback) {
  Input input;
  auto size = input.Resize(fidl_input.size);
  auto* data = input.data();
  for (size_t offset = 0; offset < size;) {
    auto status = fidl_input.socket.wait_one(
        ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED,
        zx::time::infinite(), nullptr);
    FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
    size_t actual;
    status = fidl_input.socket.read(0, &data[offset], size - offset, &actual);
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      FX_LOGS(WARNING) << "Failed to read from socket: " << zx_status_get_string(status);
      callback(status, std::move(input));
      return;
    }
    offset += actual;
  }
  callback(ZX_OK, std::move(input));
}

void Transceiver::Transmit(const Input& input, fit::function<void(FidlInput&&)> callback) {
  zx::socket sender;
  FidlInput receiver;
  receiver.size = input.size();
  auto status = zx::socket::create(ZX_SOCKET_STREAM, &sender, &receiver.socket);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  receiver.socket.shutdown(ZX_SOCKET_SHUTDOWN_WRITE);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  Pend(std::make_unique<Request>(input.Duplicate(), std::move(sender)));
  callback(std::move(receiver));
}

void Transceiver::TransmitImpl(const Input& input, zx::socket sender) {
  auto size = input.size();
  const auto* data = input.data();
  for (size_t offset = 0; offset < size;) {
    auto status =
        sender.wait_one(ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED, zx::time::infinite(), nullptr);
    FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
    size_t actual = 0;
    status = sender.write(0, &data[offset], size - offset, &actual);
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      FX_LOGS(WARNING) << "Failed to write to socket: " << zx_status_get_string(status);
      return;
    }
    offset += actual;
  }
}

}  // namespace fuzzing
