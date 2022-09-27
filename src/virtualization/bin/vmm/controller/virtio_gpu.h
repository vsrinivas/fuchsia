// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_GPU_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_GPU_H_

#include <fidl/fuchsia.virtualization.hardware/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>

#include <virtio/gpu.h>
#include <virtio/virtio_ids.h>

#include "src/virtualization/bin/vmm/virtio_device.h"

static constexpr uint16_t kVirtioGpuNumQueues = 2;

class VirtioGpu
    : public VirtioComponentDevice<VIRTIO_ID_GPU, kVirtioGpuNumQueues, virtio_gpu_config_t>,
      public fidl::WireAsyncEventHandler<fuchsia_virtualization_hardware::VirtioGpu> {
 public:
  explicit VirtioGpu(const PhysMem& phys_mem);

  zx_status_t Start(
      const zx::guest& guest,
      fidl::InterfaceHandle<fuchsia::virtualization::hardware::KeyboardListener> keyboard_listener,
      fidl::InterfaceHandle<fuchsia::virtualization::hardware::PointerListener> pointer_listener,
      ::sys::ComponentContext* context, async_dispatcher_t* dispatcher);

 private:
  enum class State {
    NOT_READY,
    CONFIG_READY,
    READY,
  } state_ = State::NOT_READY;
  std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fidl::WireSharedClient<fuchsia_virtualization_hardware::VirtioGpu> gpu_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                             zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);

  void OnConfigChanged(
      fidl::WireEvent<fuchsia_virtualization_hardware::VirtioGpu::OnConfigChanged>* event) override;
  void on_fidl_error(fidl::UnbindInfo error) override;

  void OnConfigChanged();
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_GPU_H_
