// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_VSOCK_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_VSOCK_H_

#include <fuchsia/component/cpp/fidl.h>
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
    : public VirtioComponentDevice<VIRTIO_ID_VSOCK, kVirtioVsockNumQueues, virtio_vsock_config_t>,
      public fuchsia::virtualization::HostVsockEndpoint {
 public:
  explicit VirtioVsock(const PhysMem& phys_mem);

  zx_status_t AddPublicService(sys::ComponentContext* context);

  // Only supports starting as a CFv2 component.
  zx_status_t Start(const zx::guest& guest,
                    std::vector<fuchsia::virtualization::Listener> listeners,
                    fuchsia::component::RealmSyncPtr& realm, async_dispatcher_t* dispatcher);

 private:
  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                             zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);

  // TODO(fxb/97355): Use capability routing instead of proxying through the VMM.
  // |fuchsia::virtualization::HostVsockEndpoint|
  void Connect2(uint32_t guest_port, Connect2Callback callback) override;
  void Listen(uint32_t port,
              fidl::InterfaceHandle<fuchsia::virtualization::HostVsockAcceptor> acceptor,
              ListenCallback callback) override;

  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::virtualization::hardware::VirtioVsockSyncPtr vsock_;
  fuchsia::virtualization::HostVsockEndpointPtr endpoint_;

  fuchsia::sys::ComponentControllerPtr controller_;
  fidl::BindingSet<fuchsia::virtualization::HostVsockEndpoint> bindings_;
};

}  // namespace controller

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_VSOCK_H_
