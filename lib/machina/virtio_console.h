// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_CONSOLE_H_
#define GARNET_LIB_MACHINA_VIRTIO_CONSOLE_H_

#include <iostream>

#include <virtio/console.h>

#include "garnet/lib/machina/virtio.h"

namespace machina {

class VirtioConsole : public VirtioDevice {
 public:
  VirtioConsole(const PhysMem& phys_mem);
  ~VirtioConsole() override;

  zx_status_t Start();

  virtio_queue_t* rx_queue() { return &queues_[0]; }
  virtio_queue_t* tx_queue() { return &queues_[1]; }

 private:
  static const uint16_t kNumQueues = 2;
  static_assert(kNumQueues % 2 == 0,
                "There must be a queue for both RX and TX");

  zx_status_t Transmit(virtio_queue_t* queue, uint16_t head, uint32_t* used);

  virtio_queue_t queues_[kNumQueues];
  virtio_console_config_t config_ = {};

  std::ostream& ostream_ = std::cout;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_CONSOLE_H_
