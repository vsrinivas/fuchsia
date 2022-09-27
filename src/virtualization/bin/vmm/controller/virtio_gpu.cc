// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_gpu.h"

namespace {

constexpr auto kComponentName = "virtio_gpu";
constexpr auto kComponentCollectionName = "virtio_gpu_devices";
constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_gpu#meta/virtio_gpu.cm";

}  // namespace

VirtioGpu::VirtioGpu(const PhysMem& phys_mem)
    : VirtioComponentDevice("Virtio GPU", phys_mem, 0 /* device_features */,
                            fit::bind_member(this, &VirtioGpu::ConfigureQueue),
                            fit::bind_member(this, &VirtioGpu::Ready)) {
  config_.num_scanouts = 1;
}

zx_status_t VirtioGpu::Start(
    const zx::guest& guest,
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::KeyboardListener> keyboard_listener,
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::PointerListener> pointer_listener,
    ::sys::ComponentContext* context, async_dispatcher_t* dispatcher) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_virtualization_hardware::VirtioGpu>();
  auto [client_end, server_end] = std::move(endpoints.value());
  fidl::InterfaceRequest<fuchsia::virtualization::hardware::VirtioGpu> gpu_request(
      server_end.TakeChannel());
  gpu_.Bind(std::move(client_end), dispatcher, this);

  zx_status_t status =
      CreateDynamicComponent(context, kComponentCollectionName, kComponentName, kComponentUrl,
                             [gpu_request = std::move(gpu_request)](
                                 std::shared_ptr<sys::ServiceDirectory> services) mutable {
                               return services->Connect(std::move(gpu_request));
                             });
  if (status != ZX_OK) {
    return status;
  }
  fuchsia_virtualization_hardware::wire::StartInfo start_info;
  status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }

  fidl::ClientEnd<fuchsia_virtualization_hardware::KeyboardListener> llcpp_keyboard_listener(
      keyboard_listener.TakeChannel());
  fidl::ClientEnd<fuchsia_virtualization_hardware::PointerListener> llcpp_pointer_listener(
      pointer_listener.TakeChannel());
  return gpu_.sync()
      ->Start(std::move(start_info), std::move(llcpp_keyboard_listener),
              std::move(llcpp_pointer_listener))
      .status();
}

zx_status_t VirtioGpu::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                      zx_gpaddr_t avail, zx_gpaddr_t used) {
  return gpu_.sync()->ConfigureQueue(queue, size, desc, avail, used).status();
}

zx_status_t VirtioGpu::Ready(uint32_t negotiated_features) {
  State prev_state = state_;
  state_ = State::READY;
  if (prev_state == State::CONFIG_READY) {
    OnConfigChanged();
  }
  return gpu_.sync()->Ready(negotiated_features).status();
}

void VirtioGpu::OnConfigChanged(
    fidl::WireEvent<fuchsia_virtualization_hardware::VirtioGpu::OnConfigChanged>* event) {
  OnConfigChanged();
}
void VirtioGpu::on_fidl_error(fidl::UnbindInfo error) {
  FX_LOGS(ERROR) << "Connection to VirtioGpu lost: " << error;
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
    FX_LOGS(ERROR) << "Failed to generate configuration interrupt " << status;
  }
}
