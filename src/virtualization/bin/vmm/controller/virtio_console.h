// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_CONSOLE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_CONSOLE_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/socket.h>

#include <virtio/console.h>
#include <virtio/virtio_ids.h>

#include "src/virtualization/bin/vmm/virtio_device.h"

static constexpr uint16_t kVirtioConsoleMaxNumPorts = 1;
static_assert(kVirtioConsoleMaxNumPorts > 0, "virtio-console must have at least 1 port");

// Each port has a pair of input and output virtqueues. The port 0 RX and TX
// queues always exist: other queues (including an additional per-device pair
// of control IO virtqueues) only exist if VIRTIO_CONSOLE_F_MULTIPORT is set.
static constexpr uint16_t kVirtioConsoleNumQueues =
    kVirtioConsoleMaxNumPorts == 1 ? 2 : (kVirtioConsoleMaxNumPorts + 1) * 2;
static_assert(kVirtioConsoleNumQueues % 2 == 0, "There must be a queue for both RX and TX");

class VirtioConsole : public VirtioComponentDevice<VIRTIO_ID_CONSOLE, kVirtioConsoleNumQueues,
                                                   virtio_console_config_t> {
 public:
  explicit VirtioConsole(const PhysMem& phys_mem);

  zx_status_t Start(const zx::guest& guest, zx::socket socket, ::sys::ComponentContext* context,
                    async_dispatcher_t* dispatcher);

 private:
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::virtualization::hardware::VirtioConsoleSyncPtr console_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                             zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_CONSOLE_H_
