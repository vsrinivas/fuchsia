// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_CONSOLE_H_
#define GARNET_LIB_MACHINA_VIRTIO_CONSOLE_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/socket.h>
#include <virtio/console.h>
#include <virtio/virtio_ids.h>

#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue_waiter.h"

namespace machina {

static constexpr uint16_t kVirtioConsoleNumQueues = 2;
static_assert(kVirtioConsoleNumQueues % 2 == 0,
              "There must be a queue for both RX and TX");

class VirtioConsole : public VirtioDeviceBase<VIRTIO_ID_CONSOLE,
                                              kVirtioConsoleNumQueues,
                                              virtio_console_config_t> {
 public:
  VirtioConsole(const PhysMem&, async_t* async, zx::socket socket);

  zx_status_t Start();

 private:
  zx::socket socket_;

  // Represents an single, unidirectional serial stream.
  class Stream {
   public:
    Stream(async_t* async, VirtioQueue* queue, zx_handle_t socket);
    zx_status_t Start();
    void Stop();

   private:
    zx_status_t WaitOnQueue();
    void OnQueueReady(zx_status_t status, uint16_t index);
    zx_status_t WaitOnSocket();
    void OnSocketReady(async_t* async,
                       async::WaitBase* wait,
                       zx_status_t status,
                       const zx_packet_signal_t* signal);

    void OnStreamClosed(zx_status_t status, const char* action);

    async_t* async_;
    zx_handle_t socket_;
    VirtioQueue* queue_;
    VirtioQueueWaiter queue_wait_;
    async::WaitMethod<Stream, &Stream::OnSocketReady> socket_wait_{this};
    uint16_t head_;
    virtio_desc_t desc_;
  };

  Stream rx_stream_;
  Stream tx_stream_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_CONSOLE_H_
