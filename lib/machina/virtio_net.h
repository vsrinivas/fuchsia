// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_NET_H_
#define GARNET_LIB_MACHINA_VIRTIO_NET_H_

#include <fuchsia/guest/device/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <virtio/net.h>
#include <virtio/virtio_ids.h>

#include "garnet/lib/machina/virtio_device.h"

namespace machina {

static constexpr uint16_t kVirtioNetNumQueues = 2;

static constexpr uint16_t kVirtioNetRxQueueIndex = 0;
static constexpr uint16_t kVirtioNetTxQueueIndex = 1;
static_assert(kVirtioNetRxQueueIndex != kVirtioNetTxQueueIndex,
              "RX and TX queues must be distinct");

class VirtioNet
    : public VirtioComponentDevice<VIRTIO_ID_NET, kVirtioNetNumQueues,
                                   virtio_net_config_t> {
 public:
  explicit VirtioNet(const PhysMem& phys_mem);

  zx_status_t Start(const zx::guest& guest, fuchsia::sys::Launcher* launcher,
                    async_dispatcher_t* dispatcher);

 private:
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::guest::device::VirtioNetSyncPtr net_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                             zx_gpaddr_t avail, zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_NET_H_