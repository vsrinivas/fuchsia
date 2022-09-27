// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_wl.h"

#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>

namespace {

constexpr auto kComponentName = "virtio_wl";
constexpr auto kComponentCollectionName = "virtio_wl_devices";
constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_wl#meta/virtio_wl.cm";

}  // namespace

VirtioWl::VirtioWl(const PhysMem& phys_mem)
    : VirtioComponentDevice("Virtio WL", phys_mem, VIRTIO_WL_F_TRANS_FLAGS,
                            fit::bind_member(this, &VirtioWl::ConfigureQueue),
                            fit::bind_member(this, &VirtioWl::Ready)) {}

zx_status_t VirtioWl::Start(
    const zx::guest& guest, zx::vmar vmar,
    fidl::InterfaceHandle<fuchsia::wayland::Server> wayland_server,
    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::ui::composition::Allocator> scenic_allocator,
    ::sys::ComponentContext* context, async_dispatcher_t* dispatcher) {
  zx_status_t status = CreateDynamicComponent(
      context, kComponentCollectionName, kComponentName, kComponentUrl,
      [wayland = wayland_.NewRequest()](std::shared_ptr<sys::ServiceDirectory> services) mutable {
        return services->Connect(std::move(wayland));
      });
  if (status != ZX_OK) {
    return status;
  }
  fuchsia::virtualization::hardware::StartInfo start_info;
  status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  status = wayland_->Start(std::move(start_info), std::move(vmar), std::move(wayland_server),
                           std::move(sysmem_allocator), std::move(scenic_allocator));
  return status;
}

zx_status_t VirtioWl::GetImporter(
    fidl::InterfaceRequest<fuchsia::virtualization::hardware::VirtioWaylandImporter> request) {
  return wayland_->GetImporter(std::move(request));
}

zx_status_t VirtioWl::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                     zx_gpaddr_t avail, zx_gpaddr_t used) {
  return wayland_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioWl::Ready(uint32_t negotiated_features) {
  return wayland_->Ready(negotiated_features);
}
