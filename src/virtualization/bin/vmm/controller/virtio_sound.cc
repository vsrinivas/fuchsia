// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_sound.h"

#include <lib/sys/cpp/service_directory.h>

// No features currently defined.
static constexpr uint32_t kDeviceFeatures = 0;

static constexpr char kVirtioSoundUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_sound#meta/virtio_sound.cmx";

VirtioSound::VirtioSound(const PhysMem& phys_mem)
    : VirtioComponentDevice("Virtio Sound", phys_mem, kDeviceFeatures,
                            fit::bind_member(this, &VirtioSound::ConfigureQueue),
                            fit::bind_member(this, &VirtioSound::Ready)) {}

zx_status_t VirtioSound::Start(const zx::guest& guest, fuchsia::sys::Launcher* launcher,
                               async_dispatcher_t* dispatcher, bool enable_input) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kVirtioSoundUrl;
  auto services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  services->Connect(sound_.NewRequest());

  fuchsia::virtualization::hardware::StartInfo start_info;
  zx_status_t status = PrepStart(guest, dispatcher, &start_info);
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
