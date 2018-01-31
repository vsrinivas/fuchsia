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

VirtioConsole::VirtioConsole(const PhysMem& phys_mem)
    : VirtioDevice(VIRTIO_ID_CONSOLE,
                   &config_,
                   sizeof(config_),
                   queues_,
                   kNumQueues,
                   phys_mem) {}

VirtioConsole::~VirtioConsole() = default;

zx_status_t VirtioConsole::Start() {
  auto tx_entry =
      +[](virtio_queue_t* queue, uint16_t head, uint32_t* used, void* ctx) {
        return static_cast<VirtioConsole*>(ctx)->Transmit(queue, head, used);
      };
  return virtio_queue_poll(tx_queue(), tx_entry, this, "virtio-console-tx");
}

zx_status_t VirtioConsole::Transmit(virtio_queue_t* queue,
                                    uint16_t head,
                                    uint32_t* used) {
  uint16_t index = head;
  virtio_desc_t desc;
  do {
    zx_status_t status = virtio_queue_read_desc(queue, index, &desc);
    if (status != ZX_OK) {
      return status;
    }
    ostream_.write(static_cast<const char*>(desc.addr), desc.len);

    index = desc.next;
  } while (desc.has_next);
  ostream_.flush();
  return ZX_OK;
}

}  // namespace machina
