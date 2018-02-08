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

VirtioConsole::VirtioConsole(const PhysMem& phys_mem,
                             app::ApplicationContext* application_context)
    : VirtioDevice(VIRTIO_ID_CONSOLE,
                   &config_,
                   sizeof(config_),
                   queues_,
                   kNumQueues,
                   phys_mem),
      serial_(application_context) {}

VirtioConsole::~VirtioConsole() = default;

zx_status_t VirtioConsole::Start() {
  zx_status_t status;

  auto tx_entry =
      +[](virtio_queue_t* queue, uint16_t head, uint32_t* used, void* ctx) {
        return static_cast<VirtioConsole*>(ctx)->Transmit(queue, head, used);
      };
  status = virtio_queue_poll(tx_queue(), tx_entry, this, "virtio-console-tx");
  if (status != ZX_OK) {
    return status;
  }

  auto rx_entry =
      +[](virtio_queue_t* queue, uint16_t head, uint32_t* used, void* ctx) {
        return static_cast<VirtioConsole*>(ctx)->Receive(queue, head, used);
      };
  status = virtio_queue_poll(rx_queue(), rx_entry, this, "virtio-console-rx");
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
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

    status =
        zx_socket_write(serial_.socket(), 0,
                        static_cast<const void*>(desc.addr), desc.len, nullptr);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to write to socket";
      return status;
    }

    index = desc.next;
  } while (desc.has_next);
  return ZX_OK;
}

zx_status_t VirtioConsole::Receive(virtio_queue_t* queue,
                                   uint16_t head,
                                   uint32_t* used) {
  uint16_t index = head;
  virtio_desc_t desc;
  zx_status_t status = virtio_queue_read_desc(queue, index, &desc);
  if (status != ZX_OK) {
    return status;
  }

  size_t bytes_read = 0;
  do {
    status = zx_object_wait_one(serial_.socket(), ZX_SOCKET_READABLE,
                                ZX_TIME_INFINITE, nullptr);
    if (status != ZX_OK) {
      return status;
    }

    status = zx_socket_read(serial_.socket(), 0, static_cast<void*>(desc.addr),
                            desc.len, &bytes_read);
  } while (status == ZX_ERR_SHOULD_WAIT);

  if (status != ZX_OK) {
    return status;
  }

  *used = bytes_read;
  return ZX_OK;
}

}  // namespace machina
