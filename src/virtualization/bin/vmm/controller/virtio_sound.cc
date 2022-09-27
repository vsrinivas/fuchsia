// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_sound.h"

#include <lib/sys/cpp/service_directory.h>

namespace {

// No features currently defined.
static constexpr uint32_t kDeviceFeatures = 0;

constexpr auto kComponentName = "virtio_sound";
constexpr auto kComponentCollectionName = "virtio_sound_devices";
constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_sound#meta/virtio_sound.cm";

}  // namespace

VirtioSound::VirtioSound(const PhysMem& phys_mem)
    : VirtioComponentDevice("Virtio Sound", phys_mem, kDeviceFeatures,
                            fit::bind_member(this, &VirtioSound::ConfigureQueue),
                            fit::bind_member(this, &VirtioSound::Ready)) {}

zx_status_t VirtioSound::Start(const zx::guest& guest, ::sys::ComponentContext* context,
                               async_dispatcher_t* dispatcher, bool enable_input) {
  zx_status_t status = CreateDynamicComponent(
      context, kComponentCollectionName, kComponentName, kComponentUrl,
      [sound = sound_.NewRequest()](std::shared_ptr<sys::ServiceDirectory> services) mutable {
        return services->Connect(std::move(sound));
      });
  if (status != ZX_OK) {
    return status;
  }
  fuchsia::virtualization::hardware::StartInfo start_info;
  status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t features, jacks, streams, chmaps;
  status = sound_->Start(std::move(start_info), enable_input, false /* enable_verbose_logging */,
                         &features, &jacks, &streams, &chmaps);
  if (status != ZX_OK) {
    return status;
  }

  // Must keep this constant in sync with the device implementation.
  FX_CHECK(kDeviceFeatures == features)
      << "Expected kDeviceFeatures=" << kDeviceFeatures << ", got=" << features;

  std::lock_guard<std::mutex> lock(device_config_.mutex);
  config_.jacks = jacks;
  config_.streams = streams;
  config_.chmaps = chmaps;
  return ZX_OK;
}

zx_status_t VirtioSound::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                        zx_gpaddr_t avail, zx_gpaddr_t used) {
  return sound_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioSound::Ready(uint32_t negotiated_features) {
  return sound_->Ready(negotiated_features);
}
