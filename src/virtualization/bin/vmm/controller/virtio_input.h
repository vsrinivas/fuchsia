// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_INPUT_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_INPUT_H_

#include <fuchsia/guest/device/cpp/fidl.h>
#include <virtio/input.h>
#include <virtio/virtio_ids.h>

#include "src/virtualization/bin/vmm/virtio_device.h"

static constexpr uint16_t kVirtioInputNumQueues = 2;

// Virtio input device.
class VirtioInput
    : public VirtioComponentDevice<VIRTIO_ID_INPUT, kVirtioInputNumQueues,
                                   virtio_input_config_t> {
 public:
  explicit VirtioInput(const PhysMem& phys_mem);

  zx_status_t Start(const zx::guest& guest,
                    fidl::InterfaceRequest<fuchsia::guest::device::ViewListener>
                        view_listener_request,
                    fuchsia::sys::Launcher* launcher,
                    async_dispatcher_t* dispatcher);

 private:
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::guest::device::VirtioInputSyncPtr input_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                             zx_gpaddr_t avail, zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);

  zx_status_t ConfigureDevice(uint64_t addr, const IoValue& value);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_INPUT_H_
