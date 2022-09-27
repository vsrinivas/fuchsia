// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_INPUT_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_INPUT_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>

#include <virtio/input.h>
#include <virtio/virtio_ids.h>

#include "src/virtualization/bin/vmm/virtio_device.h"

static constexpr uint16_t kVirtioInputNumQueues = 2;

using VirtioInputType = uint8_t (*)(uint8_t subsel, uint8_t* bitmap);

// Virtio input device.
class VirtioInput
    : public VirtioComponentDevice<VIRTIO_ID_INPUT, kVirtioInputNumQueues, virtio_input_config_t> {
 public:
  static uint8_t Keyboard(uint8_t subsel, uint8_t* bitmap);
  static uint8_t Pointer(uint8_t subsel, uint8_t* bitmap);

  VirtioInput(const PhysMem& phys_mem, VirtioInputType type);

  zx_status_t Start(const zx::guest& guest, ::sys::ComponentContext* context,
                    async_dispatcher_t* dispatcher, std::string component_name);

  template <typename Protocol>
  void Connect(fidl::InterfaceRequest<Protocol> request) {
    services_->Connect(std::move(request));
  }

 private:
  VirtioInputType type_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::virtualization::hardware::VirtioInputSyncPtr input_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                             zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);

  zx_status_t ConfigureDevice(uint64_t addr, const IoValue& value);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_INPUT_H_
