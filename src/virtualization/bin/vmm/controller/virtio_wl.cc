// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_wl.h"

#include <lib/fit/function.h>
#include <src/lib/fxl/logging.h>
#include <lib/svc/cpp/services.h>

static constexpr char kVirtioWlUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_wl#meta/virtio_wl.cmx";

// TODO(MAC-228): move feature flag to common location
#define VIRTIO_WL_F_MAGMA_FLAGS (1u << 2)

VirtioWl::VirtioWl(const PhysMem& phys_mem)
    : VirtioComponentDevice(phys_mem,
                            VIRTIO_WL_F_TRANS_FLAGS | VIRTIO_WL_F_MAGMA_FLAGS,
                            fit::bind_member(this, &VirtioWl::ConfigureQueue),
                            fit::bind_member(this, &VirtioWl::Ready)) {}

zx_status_t VirtioWl::Start(
    const zx::guest& guest, zx::vmar vmar,
    fidl::InterfaceHandle<fuchsia::virtualization::WaylandDispatcher>
        dispatch_handle,
    fuchsia::sys::Launcher* launcher, async_dispatcher_t* dispatcher,
    const std::string& device_path, const std::string& driver_path) {
  component::Services services;
  fuchsia::sys::LaunchInfo launch_info{
      .url = kVirtioWlUrl,
      .directory_request = services.NewRequest(),
  };
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  services.ConnectToService(wayland_.NewRequest());
  fuchsia::virtualization::hardware::StartInfo start_info;
  zx_status_t status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  status =
      wayland_->Start(std::move(start_info), std::move(vmar),
                      std::move(dispatch_handle), device_path, driver_path);
  return status;
}

zx_status_t VirtioWl::ConfigureQueue(uint16_t queue, uint16_t size,
                                     zx_gpaddr_t desc, zx_gpaddr_t avail,
                                     zx_gpaddr_t used) {
  return wayland_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioWl::Ready(uint32_t negotiated_features) {
  return wayland_->Ready(negotiated_features);
}
