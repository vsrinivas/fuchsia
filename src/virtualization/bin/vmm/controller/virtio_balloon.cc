// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_balloon.h"

#include <lib/sys/cpp/service_directory.h>

#include "src/lib/fxl/logging.h"

static constexpr char kVirtioBalloonUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_balloon#meta/virtio_balloon.cmx";

VirtioBalloon::VirtioBalloon(const PhysMem& phys_mem)
    : VirtioComponentDevice(phys_mem, VIRTIO_BALLOON_F_STATS_VQ | VIRTIO_BALLOON_F_DEFLATE_ON_OOM,
                            fit::bind_member(this, &VirtioBalloon::ConfigureQueue),
                            fit::bind_member(this, &VirtioBalloon::Ready)) {}

zx_status_t VirtioBalloon::AddPublicService(sys::ComponentContext* context) {
  return context->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

zx_status_t VirtioBalloon::Start(const zx::guest& guest, fuchsia::sys::Launcher* launcher,
                                 async_dispatcher_t* dispatcher) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kVirtioBalloonUrl;
  auto services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  services->Connect(balloon_.NewRequest());
  services->Connect(stats_.NewRequest());

  fuchsia::virtualization::hardware::StartInfo start_info;
  zx_status_t status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  return balloon_->Start(std::move(start_info));
}

zx_status_t VirtioBalloon::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                          zx_gpaddr_t avail, zx_gpaddr_t used) {
  return balloon_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioBalloon::Ready(uint32_t negotiated_features) {
  return balloon_->Ready(negotiated_features);
}

void VirtioBalloon::GetNumPages(GetNumPagesCallback callback) {
  uint32_t actual;
  {
    std::lock_guard<std::mutex> lock(device_config_.mutex);
    actual = config_.actual;
  }
  callback(actual);
}

void VirtioBalloon::RequestNumPages(uint32_t num_pages) {
  {
    std::lock_guard<std::mutex> lock(device_config_.mutex);
    config_.num_pages = num_pages;
  }
  // Send a config change interrupt to the guest.
  zx_status_t status = Interrupt(VirtioQueue::SET_CONFIG | VirtioQueue::TRY_INTERRUPT);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to generate configuration interrupt " << status;
  }
}

void VirtioBalloon::GetMemStats(GetMemStatsCallback callback) {
  stats_->GetMemStats(std::move(callback));
}
