// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_VMM_H_
#define SRC_VIRTUALIZATION_BIN_VMM_VMM_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fitx/result.h>
#include <lib/sys/cpp/component_context.h>

#include "src/virtualization/bin/vmm/controller/virtio_block.h"
#include "src/virtualization/bin/vmm/controller/virtio_console.h"
#include "src/virtualization/bin/vmm/controller/virtio_gpu.h"
#include "src/virtualization/bin/vmm/controller/virtio_input.h"
#include "src/virtualization/bin/vmm/controller/virtio_magma.h"
#include "src/virtualization/bin/vmm/controller/virtio_net.h"
#include "src/virtualization/bin/vmm/controller/virtio_rng.h"
#include "src/virtualization/bin/vmm/controller/virtio_sound.h"
#include "src/virtualization/bin/vmm/controller/virtio_wl.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/guest_impl.h"
#include "src/virtualization/bin/vmm/interrupt_controller.h"
#include "src/virtualization/bin/vmm/platform_device.h"
#include "src/virtualization/bin/vmm/uart.h"

#if __aarch64__
#include "src/virtualization/bin/vmm/arch/arm64/pl031.h"
#elif __x86_64__
#include "src/virtualization/bin/vmm/arch/x64/acpi.h"
#include "src/virtualization/bin/vmm/arch/x64/io_port.h"
#include "src/virtualization/bin/vmm/arch/x64/page_table.h"
#endif

namespace vmm {

class Vmm {
 public:
  Vmm(std::shared_ptr<async::Loop> vmm_loop, std::shared_ptr<async::Loop> device_loop)
      : vmm_loop_(std::move(vmm_loop)), device_loop_(std::move(device_loop)) {}
  ~Vmm();

  // Instantiate all VMM objects and configure the guest kernel.
  fitx::result<::fuchsia::virtualization::GuestError> Initialize(
      ::fuchsia::virtualization::GuestConfig cfg, ::sys::ComponentContext* context);

  // Start the primary VCPU. This will begin guest execution.
  fitx::result<::fuchsia::virtualization::GuestError> StartPrimaryVcpu();

 private:
#if __x86_64__
  static constexpr char kDsdtPath[] = "/pkg/data/dsdt.aml";
  static constexpr char kMcfgPath[] = "/pkg/data/mcfg.aml";
#endif

  // Used to allocate a non-overlapping device memory range.
  zx_gpaddr_t AllocDeviceAddr(size_t device_size);

  // Dispatch loops for the VMM and device controllers.
  std::shared_ptr<async::Loop> vmm_loop_;
  std::shared_ptr<async::Loop> device_loop_;

  // TODO(fxbug.dev/104989): Move this logic into this VMM object, and delete GuestImpl.
  std::unique_ptr<GuestImpl> guest_controller_;

  std::unique_ptr<Guest> guest_;

  // Platform devices.
  std::unique_ptr<InterruptController> interrupt_controller_;
  std::unique_ptr<Uart> uart_;
#if __aarch64__
  std::unique_ptr<Pl031> pl031_;
#elif __x86_64__
  std::unique_ptr<IoPort> io_port_;
#endif
  std::unique_ptr<PciBus> pci_bus_;

  // Devices.
  std::vector<std::unique_ptr<VirtioBlock>> block_devices_;
  std::unique_ptr<VirtioConsole> console_;
  std::unique_ptr<VirtioGpu> gpu_;
  std::unique_ptr<VirtioInput> input_keyboard_;
  std::unique_ptr<VirtioInput> input_pointer_;
  std::unique_ptr<VirtioRng> rng_;
  std::unique_ptr<VirtioWl> wl_;
  std::unique_ptr<VirtioMagma> magma_;
  std::unique_ptr<VirtioSound> sound_;
  std::vector<std::unique_ptr<VirtioNet>> net_devices_;

  // TODO(fxbug.dev/104989): Unowned pointers. Convert to unique_ptrs and assert on expected size.
  std::vector<PlatformDevice*> platform_devices_;

  // The start of the next valid dynamic device memory range.
  zx_gpaddr_t next_device_address_ = kFirstDynamicDeviceAddr;

  // Guest memory pointers for use in starting the primary VCPU.
  uintptr_t entry_ = 0;
  uintptr_t boot_ptr_ = 0;
};

}  // namespace vmm

#endif  // SRC_VIRTUALIZATION_BIN_VMM_VMM_H_
