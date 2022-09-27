// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_magma.h"

#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>

namespace {

constexpr auto kComponentName = "virtio_magma";
constexpr auto kComponentCollectionName = "virtio_magma_devices";
constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_magma#meta/virtio_magma.cm";

}  // namespace

VirtioMagma::VirtioMagma(const PhysMem& phys_mem)
    : VirtioComponentDevice("Virtio Magma", phys_mem, 0,
                            fit::bind_member(this, &VirtioMagma::ConfigureQueue),
                            fit::bind_member(this, &VirtioMagma::Ready)) {}

zx_status_t VirtioMagma::Start(
    const zx::guest& guest, zx::vmar vmar,
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::VirtioWaylandImporter>
        wayland_importer,
    ::sys::ComponentContext* context, async_dispatcher_t* dispatcher) {
  zx_status_t status = CreateDynamicComponent(
      context, kComponentCollectionName, kComponentName, kComponentUrl,
      [magma = magma_.NewRequest()](std::shared_ptr<sys::ServiceDirectory> services) mutable {
        return services->Connect(std::move(magma));
      });
  if (status != ZX_OK) {
    return status;
  }
  fuchsia::virtualization::hardware::StartInfo start_info;
  status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  zx_status_t start_status = ZX_ERR_INTERNAL;
  status = magma_->Start(std::move(start_info), std::move(vmar), std::move(wayland_importer),
                         &start_status);
  if (start_status != ZX_OK) {
    return start_status;
  }
  return status;
}

zx_status_t VirtioMagma::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                        zx_gpaddr_t avail, zx_gpaddr_t used) {
  return magma_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioMagma::Ready(uint32_t negotiated_features) {
  return magma_->Ready(negotiated_features);
}
