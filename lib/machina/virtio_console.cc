// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_console.h"

#include <virtio/virtio_ids.h>
#include <zircon/device/ethernet.h>

#include <fcntl.h>
#include <string.h>

#include "lib/fxl/logging.h"

namespace machina {

VirtioConsole::Stream::Stream(async_t* async,
                              VirtioQueue* queue,
                              zx_handle_t socket)
    : async_(async), socket_(socket), queue_(queue), queue_wait_(async) {
  socket_wait_.set_handler(
      fbl::BindMember(this, &VirtioConsole::Stream::OnSocketReady));
}
zx_status_t VirtioConsole::Stream::Start() {
  return WaitOnQueue();
}

void VirtioConsole::Stream::Stop() {
  socket_wait_.Cancel(async_);
  queue_wait_.Cancel();
}

zx_status_t VirtioConsole::Stream::WaitOnQueue() {
  return queue_wait_.Wait(
      queue_, fbl::BindMember(this, &VirtioConsole::Stream::OnQueueReady));
}

void VirtioConsole::Stream::OnQueueReady(zx_status_t status, uint16_t index) {
  if (status == ZX_OK) {
    head_ = index;
    status = queue_->ReadDesc(head_, &desc_);
  }
  if (status != ZX_OK) {
    OnStreamClosed(status, "reading descriptor");
    return;
  }
  status = WaitOnSocket();
  if (status != ZX_OK) {
    OnStreamClosed(status, "waiting on socket");
  }
}

zx_status_t VirtioConsole::Stream::WaitOnSocket() {
  zx_signals_t signals = ZX_SOCKET_PEER_CLOSED;
  signals |= desc_.writable ? ZX_SOCKET_READABLE : ZX_SOCKET_WRITABLE;
  socket_wait_.set_object(socket_);
  socket_wait_.set_trigger(signals);
  return socket_wait_.Begin(async_);
}

async_wait_result_t VirtioConsole::Stream::OnSocketReady(
    async_t* async,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    OnStreamClosed(status, "async wait on socket");
    return ASYNC_WAIT_FINISHED;
  }

  const bool do_read = desc_.writable;
  bool short_write = false;
  size_t actual = 0;
  if (do_read) {
    status = zx_socket_read(socket_, 0, static_cast<void*>(desc_.addr),
                            desc_.len, &actual);
  } else {
    status = zx_socket_write(socket_, 0, static_cast<const void*>(desc_.addr),
                             desc_.len, &actual);
    // It's possible only part of the descriptor has been written to the
    // socket. If so we need to wait on ZX_SOCKET_WRITABLE again to write the
    // remainder of the payload.
    if (status == ZX_OK && desc_.len > actual) {
      desc_.addr = reinterpret_cast<void*>(
          reinterpret_cast<uintptr_t>(desc_.addr) + actual);
      desc_.len -= actual;
      short_write = true;
    }
  }
  if (status == ZX_ERR_SHOULD_WAIT || short_write) {
    return ASYNC_WAIT_AGAIN;
  }
  if (status != ZX_OK) {
    OnStreamClosed(status, do_read ? "read from socket" : "write to socket");
    return ASYNC_WAIT_FINISHED;
  }
  queue_->Return(head_, do_read ? actual : 0);

  status = queue_->device()->NotifyGuest();
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to notify device " << status;
  }
  status = WaitOnQueue();
  if (status != ZX_OK) {
    OnStreamClosed(status, "wait on queue");
  }
  return ASYNC_WAIT_FINISHED;
}

void VirtioConsole::Stream::OnStreamClosed(zx_status_t status,
                                           const char* action) {
  Stop();
  FXL_LOG(ERROR) << "Stream closed during step '" << action << "' (" << status
                 << ")";
}

VirtioConsole::VirtioConsole(const PhysMem& phys_mem,
                             async_t* async,
                             zx::socket socket)
    : VirtioDeviceBase(phys_mem),
      socket_(fbl::move(socket)),
      rx_stream_(async, rx_queue(), socket_.get()),
      tx_stream_(async, tx_queue(), socket_.get()) {}

VirtioConsole::~VirtioConsole() = default;

zx_status_t VirtioConsole::Start() {
  zx_status_t status = rx_stream_.Start();
  if (status == ZX_OK) {
    status = tx_stream_.Start();
  }
  return status;
}

}  // namespace machina
