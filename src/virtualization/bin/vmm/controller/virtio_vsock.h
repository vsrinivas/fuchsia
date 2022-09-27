// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_VSOCK_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_VSOCK_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <virtio/virtio_ids.h>
#include <virtio/vsock.h>

#include "src/virtualization/bin/vmm/virtio_device.h"

namespace controller {

constexpr uint16_t kVirtioVsockNumQueues = 3;

class VirtioVsock
    : public VirtioComponentDevice<VIRTIO_ID_VSOCK, kVirtioVsockNumQueues, virtio_vsock_config_t> {
 public:
  explicit VirtioVsock(const PhysMem& phys_mem);

  // Only supports starting as a CFv2 component.
  zx_status_t Start(const zx::guest& guest,
                    std::vector<fuchsia::virtualization::Listener> listeners,
                    ::sys::ComponentContext* context, async_dispatcher_t* dispatcher);

  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint> endpoint);

 private:
  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                             zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);

  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::virtualization::hardware::VirtioVsockSyncPtr vsock_;
  fuchsia::virtualization::HostVsockEndpointPtr endpoint_;

  std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fidl::BindingSet<fuchsia::virtualization::HostVsockEndpoint> bindings_;
};

}  // namespace controller

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_VSOCK_H_
