// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_rng.h"

#include <lib/sys/cpp/service_directory.h>

namespace {

constexpr auto kComponentName = "virtio_rng";
constexpr auto kComponentCollectionName = "virtio_rng_devices";
constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_rng#meta/virtio_rng.cm";

}  // namespace

VirtioRng::VirtioRng(const PhysMem& phys_mem)
    : VirtioComponentDevice("Virtio RNG", phys_mem, 0 /* device_features */,
                            fit::bind_member(this, &VirtioRng::ConfigureQueue),
                            fit::bind_member(this, &VirtioRng::Ready)) {}

zx_status_t VirtioRng::Start(const zx::guest& guest, ::sys::ComponentContext* context,
                             async_dispatcher_t* dispatcher) {
  zx_status_t status = CreateDynamicComponent(
      context, kComponentCollectionName, kComponentName, kComponentUrl,
      [rng = rng_.NewRequest()](std::shared_ptr<sys::ServiceDirectory> services) mutable {
        return services->Connect(std::move(rng));
      });
  if (status != ZX_OK) {
    return status;
  }
  fuchsia::virtualization::hardware::StartInfo start_info;
  status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  return rng_->Start(std::move(start_info));
}

zx_status_t VirtioRng::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                      zx_gpaddr_t avail, zx_gpaddr_t used) {
  return rng_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioRng::Ready(uint32_t negotiated_features) {
  return rng_->Ready(negotiated_features);
}
