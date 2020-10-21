// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/socket.h"

#include <lib/zx/time.h>

#include <array>

static constexpr size_t kSerialBufferSize = 1024;

ZxSocket::ZxSocket(zx::socket socket) : socket_(std::move(socket)) {}

zx_status_t ZxSocket::Send(zx::time deadline, const std::string& message) {
  const char* data = message.data();
  size_t len = message.size();
  while (true) {
    // Wait until the socket is writable, is closed, or timeout occurs.
    //
    // Note "wait_one" returns ZX_OK if already signalled, even if the
    // deadline has passed.
    zx_signals_t pending = 0;
    zx_status_t status =
        socket_.wait_one(ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED, deadline, &pending);
    if (status != ZX_OK) {
      return status;
    }
    if (pending & ZX_SOCKET_PEER_CLOSED) {
      return ZX_ERR_PEER_CLOSED;
    }
    if (!(pending & ZX_SOCKET_WRITABLE)) {
      continue;
    }

    // Write out next chunk of bytes.
    size_t actual;
    status = socket_.write(0, data, len, &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      continue;
    }
    if (status != ZX_OK) {
      return status;
    }
    if (actual == len) {
      return ZX_OK;
    }
    data += actual;
    len -= actual;
  }
}

zx_status_t ZxSocket::Receive(zx::time deadline, std::string* result) {
  while (true) {
    // Wait until the socket is readable, is closed, or timeout occurs.
    //
    // Note "wait_one" returns ZX_OK if already signalled, even if the
    // deadline has passed.
    zx_signals_t pending = 0;
    zx_status_t status =
        socket_.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED, deadline, &pending);
    if (status != ZX_OK) {
      return status;
    }
    if (pending & ZX_SOCKET_PEER_CLOSED) {
      return ZX_ERR_PEER_CLOSED;
    }

    // Read a chunk of data from the socket.
    std::array<char, kSerialBufferSize> buffer;
    size_t actual;
    status = socket_.read(0, buffer.data(), buffer.size(), &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      // Retry.
      continue;
    }
    if (status != ZX_OK) {
      return status;
    }

    result->clear();
    result->append(buffer.data(), actual);
    return ZX_OK;
  }
}

zx_status_t ZxSocket::WaitForClosed(zx::time deadline) {
  zx_signals_t pending = 0;
  zx_status_t status = socket_.wait_one(ZX_SOCKET_PEER_CLOSED, deadline, &pending);
  if (status != ZX_OK) {
    return status;
  }
  return pending & ZX_SOCKET_PEER_CLOSED ? ZX_OK : ZX_ERR_BAD_STATE;
}

zx_status_t DrainSocket(SocketInterface* socket, std::string* result) {
  std::string drained_data;
  zx_status_t last_status;

  // Keep fetching data until we hit an error.
  do {
    std::string buff;
    // Perform a non-blocking receive by setting deadline
    // to ZX_TIME_INFINITE_PAST.
    last_status = socket->Receive(zx::time(ZX_TIME_INFINITE_PAST), &buff);
    drained_data.append(buff);
  } while (last_status == ZX_OK);

  // If we had a timeout, it means that no data was waiting. We still
  // consider this a successful drain.
  if (last_status == ZX_ERR_TIMED_OUT) {
    last_status = ZX_OK;
  }

  // If we received no data and got an error (other than "ZX_ERR_TIMED_OUT",
  // which we handle above), return the error. Otherwise, return success.
  bool data_fetched = !drained_data.empty();
  if (result != nullptr) {
    *result = std::move(drained_data);
  }
  if (!data_fetched) {
    return last_status;
  }
  return ZX_OK;
}
