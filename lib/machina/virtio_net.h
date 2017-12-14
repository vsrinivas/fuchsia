// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_NET_H_
#define GARNET_LIB_MACHINA_NET_H_

#include <fbl/unique_fd.h>
#include <virtio/net.h>

#include "garnet/lib/machina/virtio.h"

namespace machina {

// Implements a Virtio Ethernet device.
class VirtioNet : public VirtioDevice {
 public:
  VirtioNet(const PhysMem& phys_mem);
  ~VirtioNet() override;

  // Starts a thread to monitor for Ethernet devices, and begins execution of
  // the Virtio Ethernet device once it finds one.
  zx_status_t Start();

  // Drains a Virtio queue, and passes data to the underlying Ethernet device.
  zx_status_t DrainQueue(virtio_queue_t* queue,
                         uint32_t num_entries,
                         zx_handle_t fifo);

  virtio_queue_t* rx_queue() { return &queues_[0]; }
  virtio_queue_t* tx_queue() { return &queues_[1]; }

 private:
  // This must be a multiple of 2 for the RX and TX queues.
  static const uint16_t kNumQueues = 2;

  // Queue for handling block requests.
  virtio_queue_t queues_[kNumQueues];
  // Device configuration fields.
  virtio_net_config_t config_ = {};
  // Ethernet control plane.
  eth_fifos_t fifos_ = {};
  // Connection to the Ethernet device.
  fbl::unique_fd net_fd_;

  // Starts the Virtio Ethernet device.
  zx_status_t StartDevice(int dir_fd, int event, const char* fn);

  zx_status_t ReceiveLoop();
  zx_status_t TransmitLoop();
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_NET_H_
