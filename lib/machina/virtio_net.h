// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_NET_H_
#define GARNET_LIB_MACHINA_VIRTIO_NET_H_

#include <fbl/unique_fd.h>
#include <lib/async/cpp/wait.h>
#include <virtio/net.h>

#include "garnet/lib/machina/virtio_device.h"

namespace machina {

// Implements a Virtio Ethernet device.
class VirtioNet : public VirtioDevice {
 public:
  VirtioNet(const PhysMem& phys_mem, async_t* async);
  ~VirtioNet() override;

  // Starts the Virtio Ethernet device based on the path provided.
  zx_status_t Start(const char* path);

  // Drains a Virtio queue, and passes data to the underlying Ethernet device.
  zx_status_t DrainQueue(VirtioQueue* queue,
                         uint32_t max_entries,
                         zx_handle_t fifo,
                         bool rx);

  VirtioQueue* rx_queue() { return &queues_[0]; }
  VirtioQueue* tx_queue() { return &queues_[1]; }

  zx_status_t WaitOnFifos(const eth_fifos_t& fifos);

 private:
  static constexpr uint16_t kNumQueues = 2;
  static_assert(kNumQueues % 2 == 0,
                "There must be a queue for both RX and TX");

  // Queue for handling block requests.
  VirtioQueue queues_[kNumQueues];
  // Device configuration fields.
  virtio_net_config_t config_ = {};
  // Ethernet control plane.
  eth_fifos_t fifos_ = {};
  // Connection to the Ethernet device.
  fbl::unique_fd net_fd_;

  zx_status_t ReceiveLoop();
  zx_status_t TransmitLoop();

  zx_status_t FifoWait(async::Wait* wait,
                       zx_handle_t fifo,
                       size_t fifo_depth,
                       VirtioQueue* queue);

  // Reads entries out of a FIFO and returns the corresponding descriptors
  // to the queue.
  zx_status_t DrainFifo(zx_handle_t fifo,
                        size_t fifo_depth,
                        VirtioQueue* queue);

  async_t* async_;
  async::Wait tx_wait_;
  async::Wait rx_wait_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_NET_H_
