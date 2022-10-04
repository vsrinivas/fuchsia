// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_VMM_H_
#define SRC_VIRTUALIZATION_BIN_VMM_VMM_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fitx/result.h>
#include <lib/sys/cpp/component_context.h>

#include "src/virtualization/bin/vmm/controller/virtio_balloon.h"
#include "src/virtualization/bin/vmm/controller/virtio_block.h"
#include "src/virtualization/bin/vmm/controller/virtio_console.h"
#include "src/virtualization/bin/vmm/controller/virtio_gpu.h"
#include "src/virtualization/bin/vmm/controller/virtio_input.h"
#include "src/virtualization/bin/vmm/controller/virtio_magma.h"
#include "src/virtualization/bin/vmm/controller/virtio_net.h"
#include "src/virtualization/bin/vmm/controller/virtio_rng.h"
#include "src/virtualization/bin/vmm/controller/virtio_sound.h"
#include "src/virtualization/bin/vmm/controller/virtio_vsock.h"
#include "src/virtualization/bin/vmm/controller/virtio_wl.h"
#include "src/virtualization/bin/vmm/guest.h"
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

class Vmm : public fuchsia::virtualization::Guest {
 public:
  Vmm() = default;
  ~Vmm() override;

  // Instantiate all VMM objects and configure the guest kernel.
  virtual fitx::result<::fuchsia::virtualization::GuestError> Initialize(
      ::fuchsia::virtualization::GuestConfig cfg, ::sys::ComponentContext* context,
      async_dispatcher_t* dispatcher);

  // Start the primary VCPU. This will begin guest execution.
  virtual fitx::result<::fuchsia::virtualization::GuestError> StartPrimaryVcpu(
      fit::function<void(fitx::result<::fuchsia::virtualization::GuestError>)> stop_callback);

  // The guest is being shutdown, so notify all clients by disconnecting with an epitaph.
  virtual void NotifyClientsShutdown(zx_status_t status);

  // |fuchsia::virtualization::Guest|
  void GetSerial(GetSerialCallback callback) override;
  void GetConsole(GetConsoleCallback callback) override;
  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint> endpoint,
      GetHostVsockEndpointCallback callback) override;
  void GetBalloonController(
      fidl::InterfaceRequest<fuchsia::virtualization::BalloonController> endpoint,
      GetBalloonControllerCallback callback) override;

 private:
#if __x86_64__
  static constexpr char kDsdtPath[] = "/pkg/data/dsdt.aml";
  static constexpr char kMcfgPath[] = "/pkg/data/mcfg.aml";
#endif

  // Allocates a non-overlapping device memory range.
  zx_gpaddr_t AllocDeviceAddr(size_t device_size);

  // Serve any supported public services. This will always serve |fuchsia::virtualization::Guest|.
  fitx::result<::fuchsia::virtualization::GuestError> AddPublicServices();

  // Must be destroyed first (see comment in destructor).
  std::unique_ptr<::Guest> guest_;

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
  std::unique_ptr<VirtioBalloon> balloon_;
  std::vector<std::unique_ptr<VirtioBlock>> block_devices_;
  std::unique_ptr<VirtioConsole> console_;
  std::unique_ptr<VirtioGpu> gpu_;
  std::unique_ptr<VirtioInput> input_keyboard_;
  std::unique_ptr<VirtioInput> input_pointer_;
  std::unique_ptr<VirtioRng> rng_;
  std::unique_ptr<controller::VirtioVsock> vsock_;
  std::unique_ptr<VirtioWl> wl_;
  std::unique_ptr<VirtioMagma> magma_;
  std::unique_ptr<VirtioSound> sound_;
  std::vector<std::unique_ptr<VirtioNet>> net_devices_;

  std::vector<PlatformDevice*> platform_devices_;  // Unowned pointers.

  // The start of the next valid dynamic device memory range.
  zx_gpaddr_t next_device_address_ = kFirstDynamicDeviceAddr;

  // Guest memory pointers for use in starting the primary VCPU.
  uintptr_t entry_ = 0;
  uintptr_t boot_ptr_ = 0;

  // Client ends for the serial and console sockets. Serial will always be available, and console
  // will be available only when the virtio console device was enabled via the guest configuration.
  zx::socket client_serial_socket_;
  zx::socket client_console_socket_;

  std::shared_ptr<sys::OutgoingDirectory> outgoing_;
  fidl::BindingSet<fuchsia::virtualization::Guest> guest_bindings_;
};

}  // namespace vmm

#endif  // SRC_VIRTUALIZATION_BIN_VMM_VMM_H_
