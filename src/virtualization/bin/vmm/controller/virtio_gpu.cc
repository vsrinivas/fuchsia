// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_gpu.h"

static constexpr char kVirtioGpuUrl[] = "fuchsia-pkg://fuchsia.com/virtio_gpu#meta/virtio_gpu.cmx";

VirtioGpu::VirtioGpu(const PhysMem& phys_mem)
    : VirtioComponentDevice(phys_mem, 0 /* device_features */,
                            fit::bind_member(this, &VirtioGpu::ConfigureQueue),
                            fit::bind_member(this, &VirtioGpu::Ready)) {
  config_.num_scanouts = 1;
}

zx_status_t VirtioGpu::Start(
    const zx::guest& guest,
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::ViewListener> view_listener,
    fuchsia::sys::Launcher* launcher, async_dispatcher_t* dispatcher) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kVirtioGpuUrl;
  services_ = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  services_->Connect(gpu_.NewRequest());
  services_->Connect(events_.NewRequest());
  events_.events().OnConfigChanged = fit::bind_member(this, &VirtioGpu::OnConfigChanged);

  fuchsia::virtualization::hardware::StartInfo start_info;
  zx_status_t status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  return gpu_->Start(std::move(start_info), std::move(view_listener));
}

zx_status_t VirtioGpu::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                      zx_gpaddr_t avail, zx_gpaddr_t used) {
  return gpu_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioGpu::Ready(uint32_t negotiated_features) {
  State prev_state = state_;
  state_ = State::READY;
  if (prev_state == State::CONFIG_READY) {
    OnConfigChanged();
  }
  return gpu_->Ready(negotiated_features);
}

void VirtioGpu::OnConfigChanged() {
  if (state_ != State::READY) {
    state_ = State::CONFIG_READY;
    return;
  }
  {
    std::lock_guard<std::mutex> lock(device_config_.mutex);
    config_.events_read |= VIRTIO_GPU_EVENT_DISPLAY;
  }
  // Send a config change interrupt to the guest.
  zx_status_t status = Interrupt(VirtioQueue::SET_CONFIG | VirtioQueue::TRY_INTERRUPT);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to generate configuration interrupt " << status;
  }
}
