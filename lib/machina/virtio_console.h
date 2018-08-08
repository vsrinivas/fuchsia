// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_CONSOLE_H_
#define GARNET_LIB_MACHINA_VIRTIO_CONSOLE_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/socket.h>
#include <virtio/console.h>
#include <virtio/virtio_ids.h>
#include <array>

#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue_waiter.h"

namespace machina {

static constexpr uint16_t kVirtioConsoleMaxNumPorts = 1;
static_assert(kVirtioConsoleMaxNumPorts > 0,
              "virtio-console must have at least 1 port");

// Each port has a pair of input and output virtqueues. The port 0 RX and TX
// queues always exist: other queues (including an additional per-device pair
// of control IO virtqueues) only exist if VIRTIO_CONSOLE_F_MULTIPORT is set.
static constexpr uint16_t kVirtioConsoleNumQueues =
    kVirtioConsoleMaxNumPorts == 1 ? 2 : (kVirtioConsoleMaxNumPorts + 1) * 2;
static_assert(kVirtioConsoleNumQueues % 2 == 0,
              "There must be a queue for both RX and TX");

class VirtioConsole
    : public VirtioDeviceBase<VIRTIO_ID_CONSOLE, kVirtioConsoleNumQueues,
                              virtio_console_config_t> {
 public:
  VirtioConsole(const PhysMem&, async_dispatcher_t* dispatcher,
                zx::socket socket);
  ~VirtioConsole();

  zx_status_t Start();

 private:
  class Port;

  fbl::Mutex mutex_;
  std::array<std::unique_ptr<Port>, kVirtioConsoleMaxNumPorts> ports_
      __TA_GUARDED(mutex_);
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_CONSOLE_H_
