// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_NET_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_NET_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <virtio/net.h>
#include <virtio/virtio_ids.h>

#include "src/virtualization/bin/vmm/virtio_device.h"

static constexpr uint16_t kVirtioNetNumQueues = 2;

static constexpr uint16_t kVirtioNetRxQueueIndex = 0;
static constexpr uint16_t kVirtioNetTxQueueIndex = 1;
static_assert(kVirtioNetRxQueueIndex != kVirtioNetTxQueueIndex,
              "RX and TX queues must be distinct");

class VirtioNet
    : public VirtioComponentDevice<VIRTIO_ID_NET, kVirtioNetNumQueues, virtio_net_config_t> {
 public:
  explicit VirtioNet(const PhysMem& phys_mem);

  zx_status_t Start(const zx::guest& guest,
                    const fuchsia::hardware::ethernet::MacAddress& mac_address, bool enable_bridge,
                    ::sys::ComponentContext* context, async_dispatcher_t* dispatcher,
                    size_t component_name_suffix);

 private:
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::virtualization::hardware::VirtioNetSyncPtr net_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                             zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_NET_H_
