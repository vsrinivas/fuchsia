// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/trace-provider/provider.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include <atomic>
#include <unordered_map>
#include <vector>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/vmm/controller/virtio_balloon.h"
#include "src/virtualization/bin/vmm/controller/virtio_block.h"
#include "src/virtualization/bin/vmm/controller/virtio_console.h"
#include "src/virtualization/bin/vmm/controller/virtio_gpu.h"
#include "src/virtualization/bin/vmm/controller/virtio_input.h"
#include "src/virtualization/bin/vmm/controller/virtio_magma.h"
#include "src/virtualization/bin/vmm/controller/virtio_net.h"
#include "src/virtualization/bin/vmm/controller/virtio_rng.h"
#include "src/virtualization/bin/vmm/controller/virtio_wl.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/guest_impl.h"
#include "src/virtualization/bin/vmm/interrupt_controller.h"
#include "src/virtualization/bin/vmm/linux.h"
#include "src/virtualization/bin/vmm/pci.h"
#include "src/virtualization/bin/vmm/platform_device.h"
#include "src/virtualization/bin/vmm/uart.h"
#include "src/virtualization/bin/vmm/vcpu.h"
#include "src/virtualization/bin/vmm/virtio_vsock.h"
#include "src/virtualization/bin/vmm/zircon.h"

#if __aarch64__
#include "src/virtualization/bin/vmm/arch/arm64/pl031.h"
#elif __x86_64__
#include "src/virtualization/bin/vmm/arch/x64/acpi.h"
#include "src/virtualization/bin/vmm/arch/x64/io_port.h"
#include "src/virtualization/bin/vmm/arch/x64/page_table.h"
static constexpr char kDsdtPath[] = "/pkg/data/dsdt.aml";
static constexpr char kMcfgPath[] = "/pkg/data/mcfg.aml";
#else
#error Unknown architecture.
#endif

// For devices that can have their addresses anywhere we run a dynamic
// allocator that starts fairly high in the guest physical address space.
static constexpr zx_gpaddr_t kFirstDynamicDeviceAddr = 0xc00000000;

static zx_gpaddr_t AllocDeviceAddr(size_t device_size) {
  static zx_gpaddr_t next_device_addr = kFirstDynamicDeviceAddr;
  zx_gpaddr_t ret = next_device_addr;
  next_device_addr += device_size;
  return ret;
}

int main(int argc, char** argv) {
  syslog::SetTags({"vmm"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Loop device_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  fuchsia::virtualization::GuestConfig cfg;
  fuchsia::virtualization::GuestConfigProviderSyncPtr cfg_provider;
  context->svc()->Connect(cfg_provider.NewRequest());
  zx_status_t status = cfg_provider->Get(&cfg);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "No launch info provided";
    return status;
  }

  GuestImpl guest_controller;
  fuchsia::sys::LauncherPtr launcher;
  context->svc()->Connect(launcher.NewRequest());

  DevMem dev_mem;
  for (const fuchsia::virtualization::MemorySpec& spec : cfg.memory()) {
    // Avoid a collision between static and dynamic address assignment.
    if (spec.base + spec.size > kFirstDynamicDeviceAddr) {
      FX_LOGS(ERROR) << "Requested memory should be less than " << kFirstDynamicDeviceAddr;
      return ZX_ERR_INVALID_ARGS;
    }
    // Add device memory range.
    if (spec.policy == fuchsia::virtualization::MemoryPolicy::HOST_DEVICE &&
        !dev_mem.AddRange(spec.base, spec.size)) {
      FX_LOGS(ERROR) << "Failed to add device memory at 0x" << std::hex << spec.base;
      return ZX_ERR_INTERNAL;
    }
  }

  Guest guest;
  status = guest.Init(cfg.memory());
  if (status != ZX_OK) {
    return status;
  }

  std::vector<PlatformDevice*> platform_devices;

  // Setup UARTs.
  Uart uart(guest_controller.SerialSocket());
  status = uart.Init(&guest);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create UART";
    return status;
  }
  platform_devices.push_back(&uart);
  // Setup interrupt controller.
  InterruptController interrupt_controller(&guest);
#if __aarch64__
  status = interrupt_controller.Init(cfg.cpus(), cfg.interrupts());
#elif __x86_64__
  status = interrupt_controller.Init();
#else
#error Unknown architecture.
#endif
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create interrupt controller";
    return status;
  }
  platform_devices.push_back(&interrupt_controller);

#if __aarch64__
  // Setup PL031 RTC.
  Pl031 pl031;
  status = pl031.Init(&guest);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create PL031 RTC";
    return status;
  }
  platform_devices.push_back(&pl031);
#elif __x86_64__
  // Setup IO ports.
  IoPort io_port;
  status = io_port.Init(&guest);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create IO ports";
    return status;
  }
#else
#error Unknown architecture.
#endif

  // Setup PCI.
  PciBus bus(&guest, &interrupt_controller);
  status = bus.Init(device_loop.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create PCI bus";
    return status;
  }
  platform_devices.push_back(&bus);

  // Setup balloon device.
  VirtioBalloon balloon(guest.phys_mem());
  if (cfg.virtio_balloon()) {
    status = bus.Connect(balloon.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect balloon device";
      return status;
    }
    status = balloon.Start(guest.object(), launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start balloon device";
      return status;
    }
  }

  // Create a new VirtioBlock device for each device requested.
  std::vector<std::unique_ptr<VirtioBlock>> block_devices;
  for (auto& block_device : *cfg.mutable_block_devices()) {
    auto block = std::make_unique<VirtioBlock>(guest.phys_mem(), block_device.mode);
    status = bus.Connect(block->pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect block device";
      return status;
    }
    status = block->Start(guest.object(), std::move(block_device.id), block_device.format,
                          std::move(block_device.file), launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start block device";
      return status;
    }
    block_devices.push_back(std::move(block));
  }

  // Setup console device.
  VirtioConsole console(guest.phys_mem());
  if (cfg.virtio_console()) {
    status = bus.Connect(console.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect console device";
      return status;
    }
    status = console.Start(guest.object(), guest_controller.ConsoleSocket(), launcher.get(),
                           device_loop.dispatcher());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start console device";
      return status;
    }
  }

  VirtioGpu gpu(guest.phys_mem());
  VirtioInput input_keyboard(guest.phys_mem(), VirtioInput::Keyboard);
  VirtioInput input_pointer(guest.phys_mem(), VirtioInput::Pointer);
  if (cfg.virtio_gpu()) {
    // Setup keyboard device.
    status = bus.Connect(input_keyboard.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect keyboard device";
      return status;
    }
    status = input_keyboard.Start(guest.object(), launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start keyboard device";
      return status;
    }
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::KeyboardListener> keyboard_listener;
    input_keyboard.Connect(keyboard_listener.NewRequest());

    // Setup pointer device.
    status = bus.Connect(input_pointer.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect mouse device";
      return status;
    }
    status = input_pointer.Start(guest.object(), launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start mouse device";
      return status;
    }
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::PointerListener> pointer_listener;
    input_pointer.Connect(pointer_listener.NewRequest());

    // Setup GPU device.
    status = bus.Connect(gpu.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect GPU device";
      return status;
    }
    status = gpu.Start(guest.object(), std::move(keyboard_listener), std::move(pointer_listener),
                       launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start GPU device";
      return status;
    }
  }

  // Setup RNG device.
  VirtioRng rng(guest.phys_mem());
  if (cfg.virtio_rng()) {
    status = bus.Connect(rng.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect RNG device";
      return status;
    }
    status = rng.Start(guest.object(), launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start RNG device";
      return status;
    }
  }

  // Setup vsock device. Vsock uses its own dispatcher as a temporary measure
  // until it is moved out of process.
  async::Loop vsock_loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  VirtioVsock vsock(context.get(), guest.phys_mem(), vsock_loop.dispatcher());
  if (cfg.virtio_vsock()) {
    status = bus.Connect(vsock.pci_device(), vsock_loop.dispatcher(), false);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect Vsock device";
      return status;
    }
    status = vsock_loop.StartThread("vsock-handler");
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to create vsock async worker";
      return status;
    }
  }

  // Setup wayland device.
  VirtioWl wl(guest.phys_mem());
  if (cfg.has_wayland_device()) {
    size_t wl_dev_mem_size = cfg.wayland_device().memory;
    zx_gpaddr_t wl_dev_mem_offset = AllocDeviceAddr(wl_dev_mem_size);
    if (!dev_mem.AddRange(wl_dev_mem_offset, wl_dev_mem_size)) {
      FX_LOGS(INFO) << "Could not reserve device memory range for wayland device";
      return status;
    }
    zx::vmar wl_vmar;
    status = guest.CreateSubVmar(wl_dev_mem_offset, wl_dev_mem_size, &wl_vmar);
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Could not create VMAR for wayland device";
      return status;
    }
    status = bus.Connect(wl.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Could not connect wayland device";
      return status;
    }
    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem_allocator = nullptr;
    status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                  sysmem_allocator.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Could not connect to sysmem allocator service";
      return status;
    }
    fidl::InterfaceHandle<fuchsia::ui::composition::Allocator> scenic_allocator = nullptr;
    status = fdio_service_connect("/svc/fuchsia.ui.composition.Allocator",
                                  scenic_allocator.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Could not connect to scenic allocator service";
      return status;
    }
    status =
        wl.Start(guest.object(), std::move(wl_vmar),
                 std::move(cfg.mutable_wayland_device()->dispatcher), std::move(sysmem_allocator),
                 std::move(scenic_allocator), launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Could not start wayland device";
      return status;
    }
  }

  // Setup magma device.
  VirtioMagma magma(guest.phys_mem());
  if (cfg.has_magma_device()) {
    // TODO(fxbug.dev/12619): simplify vmm launch configs
    size_t magma_dev_mem_size = cfg.magma_device().memory;
    zx_gpaddr_t magma_dev_mem_offset = AllocDeviceAddr(magma_dev_mem_size);
    if (!dev_mem.AddRange(magma_dev_mem_offset, magma_dev_mem_size)) {
      FX_PLOGS(INFO, status) << "Could not reserve device memory range for magma device";
      return status;
    }
    zx::vmar magma_vmar;
    status = guest.CreateSubVmar(magma_dev_mem_offset, magma_dev_mem_size, &magma_vmar);
    if (status != ZX_OK) {
      FX_PLOGS(INFO, status) << "Could not create VMAR for magma device";
      return status;
    }
    status = bus.Connect(magma.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FX_PLOGS(INFO, status) << "Could not connect magma device";
      return status;
    }
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::VirtioWaylandImporter>
        wayland_importer_handle = nullptr;
    if (cfg.has_wayland_device()) {
      status = wl.GetImporter(wayland_importer_handle.NewRequest());
      if (status != ZX_OK) {
        FX_PLOGS(INFO, status) << "Could not get wayland importer";
        return status;
      }
    }
    status = magma.Start(guest.object(), std::move(magma_vmar), std::move(wayland_importer_handle),
                         launcher.get(), device_loop.dispatcher());
    if (status == ZX_ERR_NOT_FOUND) {
      FX_LOGS(INFO) << "Magma device not supported by host";
    } else if (status != ZX_OK) {
      FX_PLOGS(INFO, status) << "Could not start magma device";
      return status;
    }
  }

  // Setup net device.
  // We setup networking last, as this can cause a temporary loss of network
  // access as we configure the bridge. If networking is lost while loading
  // packages for devices, the VMM will fail.
  std::vector<std::unique_ptr<VirtioNet>> net_devices;
  // NOTE(abdulla): We use mutable_net_devices() here so that we force default
  // initialization of the field if it has not been previously set, thus
  // avoiding an assertion failure.
  for (auto& net_device : *cfg.mutable_net_devices()) {
    auto net = std::make_unique<VirtioNet>(guest.phys_mem());
    status = bus.Connect(net->pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect Ethernet device";
      return status;
    }
    status = net->Start(guest.object(), net_device.mac_address, net_device.enable_bridge,
                        launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Could not open Ethernet device";
      return status;
    }
    net_devices.push_back(std::move(net));
  }

#if __x86_64__
  status = create_page_table(guest.phys_mem());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create page table";
    return status;
  }

  AcpiConfig acpi_cfg = {
      .dsdt_path = kDsdtPath,
      .mcfg_path = kMcfgPath,
      .io_apic_addr = IoApic::kPhysBase,
      .cpus = cfg.cpus(),
  };
  status = create_acpi_table(acpi_cfg, guest.phys_mem());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create ACPI table";
    return status;
  }
#endif  // __x86_64__

  // Add any trap ranges as device memory.
  for (const IoMapping& mapping : guest.mappings()) {
    if ((mapping.kind() == ZX_GUEST_TRAP_MEM || mapping.kind() == ZX_GUEST_TRAP_BELL) &&
        !dev_mem.AddRange(mapping.base(), mapping.size())) {
      FX_LOGS(ERROR) << "Failed to add trap range as device memory";
      return ZX_ERR_INTERNAL;
    }
  }

  // Setup kernel.
  uintptr_t entry = 0;
  uintptr_t boot_ptr = 0;
  switch (cfg.kernel_type()) {
    case fuchsia::virtualization::KernelType::ZIRCON:
      status = setup_zircon(&cfg, guest.phys_mem(), dev_mem, platform_devices, &entry, &boot_ptr);
      break;
    case fuchsia::virtualization::KernelType::LINUX:
      status = setup_linux(&cfg, guest.phys_mem(), dev_mem, platform_devices, &entry, &boot_ptr);
      break;
    default:
      FX_LOGS(ERROR) << "Unknown kernel";
      return ZX_ERR_INVALID_ARGS;
  }
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to load kernel";
    return status;
  }

  // Setup primary VCPU.
  status = guest.StartVcpu(0 /* id */, entry, boot_ptr, &loop);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to start VCPU-0";
    loop.Quit();
  }

  status = guest_controller.AddPublicService(context.get());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to add guest controller public service";
    loop.Quit();
  }
  status = balloon.AddPublicService(context.get());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to add balloon public service";
    loop.Quit();
  }
  status = gpu.AddPublicService(context.get());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to add GPU public service";
    loop.Quit();
  }

  // Start the dispatch thread for communicating with the out of process
  // devices.
  status = device_loop.StartThread("device-worker");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create async worker";
    return status;
  }

  return loop.Run();
}
