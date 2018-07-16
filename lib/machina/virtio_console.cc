// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_console.h"

#include <virtio/virtio_ids.h>

#include <fcntl.h>
#include <string.h>

#include "lib/fxl/logging.h"

namespace machina {

// Represents an single, unidirectional serial stream.
class Stream {
 public:
  Stream(async_dispatcher_t* dispatcher, VirtioQueue* queue, zx_handle_t socket)
      : dispatcher_(dispatcher),
        socket_(socket),
        queue_(queue),
        queue_wait_(dispatcher, queue,
                    fit::bind_member(this, &Stream::OnQueueReady)) {}

  zx_status_t Start() { return WaitOnQueue(); }

  void Stop() {
    socket_wait_.Cancel();
    queue_wait_.Cancel();
  }

 private:
  zx_status_t WaitOnQueue() { return queue_wait_.Begin(); }

  void OnQueueReady(zx_status_t status, uint16_t index) {
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

  zx_status_t WaitOnSocket() {
    zx_signals_t signals = ZX_SOCKET_PEER_CLOSED;
    signals |= desc_.writable ? ZX_SOCKET_READABLE : ZX_SOCKET_WRITABLE;
    socket_wait_.set_object(socket_);
    socket_wait_.set_trigger(signals);
    return socket_wait_.Begin(dispatcher_);
  }

  void OnSocketReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                     zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      status = queue_->Return(head_, 0);
      if (status != ZX_OK) {
        FXL_LOG(WARNING) << "Failed to return descriptor " << status;
      }
      status = WaitOnQueue();
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
      status = wait->Begin(dispatcher);
      if (status != ZX_OK) {
        OnStreamClosed(status, "dispatcher wait on socket");
      }
      return;
    }
    if (status != ZX_OK) {
      OnStreamClosed(status, do_read ? "read from socket" : "write to socket");
      return;
    }
    status = queue_->Return(head_, do_read ? actual : 0);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "Failed to return descriptor " << status;
    }
    status = WaitOnQueue();
    if (status != ZX_OK) {
      OnStreamClosed(status, "wait on queue");
    }
  }

  void OnStreamClosed(zx_status_t status, const char* action) {
    Stop();
    FXL_LOG(ERROR) << "Stream closed during step '" << action << "' (" << status
                   << ")";
  }

  async_dispatcher_t* dispatcher_;
  zx_handle_t socket_;
  VirtioQueue* queue_;
  VirtioQueueWaiter queue_wait_;
  async::WaitMethod<Stream, &Stream::OnSocketReady> socket_wait_{this};
  uint16_t head_;
  virtio_desc_t desc_;
};

class VirtioConsole::Port {
 public:
  Port(async_dispatcher_t* dispatcher, VirtioQueue* rx_queue,
       VirtioQueue* tx_queue, zx::socket socket)
      : socket_(std::move(socket)),
        rx_stream_(dispatcher, rx_queue, socket_.get()),
        tx_stream_(dispatcher, tx_queue, socket_.get()) {}

  zx_status_t Start() {
    zx_status_t status = rx_stream_.Start();
    if (status != ZX_OK) {
      return status;
    }
    return tx_stream_.Start();
  }

 private:
  zx::socket socket_;
  Stream rx_stream_;
  Stream tx_stream_;
};

VirtioConsole::VirtioConsole(const PhysMem& phys_mem,
                             async_dispatcher_t* dispatcher, zx::socket socket)
    : VirtioDeviceBase(phys_mem) {
  {
    fbl::AutoLock lock(&config_mutex_);
    config_.max_nr_ports = kVirtioConsoleMaxNumPorts;
  }
  ports_[0] =
      std::make_unique<Port>(dispatcher, queue(0), queue(1), std::move(socket));
}

VirtioConsole::~VirtioConsole() = default;

zx_status_t VirtioConsole::Start() {
  fbl::AutoLock lock(&mutex_);
  return ports_[0]->Start();
}

}  // namespace machina
