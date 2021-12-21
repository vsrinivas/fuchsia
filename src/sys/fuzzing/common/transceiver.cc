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

Transceiver::Transceiver() : close_([this]() { CloseImpl(); }), join_([this]() { JoinImpl(); }) {
  worker_ = std::thread([this]() { Worker(); });
}

Transceiver::~Transceiver() {
  Close();
  Join();
}

zx_status_t Transceiver::Pend(std::unique_ptr<Request> request) {
  bool stopped;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped = stopped_;
    if (!stopped) {
      stopped_ = request->type == Request::kStop;
      requests_.push_back(std::move(request));
      sync_.Signal();
    }
  }
  if (stopped) {
    if (request->type == Request::kReceive) {
      request->rx_callback(ZX_ERR_BAD_STATE, Input());
    }
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

void Transceiver::Worker() {
  while (true) {
    std::unique_ptr<Request> request;
    // Wait indefinitely. Destroying this object will send |kStop|.
    sync_.WaitFor("more data to transfer");
    {
      std::lock_guard<std::mutex> lock(mutex_);
      request = std::move(requests_.front());
      requests_.pop_front();
      if (requests_.empty()) {
        sync_.Reset();
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
  Waiter waiter = [&fidl_input](zx::time deadline) {
    auto flags = ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED;
    return fidl_input.socket.wait_one(flags, deadline, nullptr);
  };
  for (size_t offset = 0; offset < size;) {
    auto status = WaitFor("socket to become readable", &waiter);
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

zx_status_t Transceiver::Transmit(Input input, FidlInput* out_fidl_input) {
  zx::socket sender;
  FidlInput receiver;
  receiver.size = input.size();
  auto status = zx::socket::create(ZX_SOCKET_STREAM, &sender, &receiver.socket);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  receiver.socket.set_disposition(ZX_SOCKET_DISPOSITION_WRITE_DISABLED, 0);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  status = Pend(std::make_unique<Request>(std::move(input), std::move(sender)));
  if (status == ZX_OK) {
    *out_fidl_input = std::move(receiver);
  }
  return status;
}

void Transceiver::TransmitImpl(const Input& input, zx::socket sender) {
  auto size = input.size();
  const auto* data = input.data();
  Waiter waiter = [&sender](zx::time deadline) {
    return sender.wait_one(ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED, deadline, nullptr);
  };
  for (size_t offset = 0; offset < size;) {
    auto status = WaitFor("socket to become writable", &waiter);
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

void Transceiver::CloseImpl() { Pend(std::make_unique<Request>()); }

void Transceiver::JoinImpl() {
  FX_DCHECK(worker_.joinable());
  worker_.join();
}

}  // namespace fuzzing
