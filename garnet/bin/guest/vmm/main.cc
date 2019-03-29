// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <unordered_map>
#include <vector>

#include <fuchsia/guest/vmm/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <trace-provider/provider.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/bin/guest/vmm/controller/virtio_balloon.h"
#include "garnet/bin/guest/vmm/controller/virtio_block.h"
#include "garnet/bin/guest/vmm/controller/virtio_console.h"
#include "garnet/bin/guest/vmm/controller/virtio_gpu.h"
#include "garnet/bin/guest/vmm/controller/virtio_input.h"
#include "garnet/bin/guest/vmm/controller/virtio_net.h"
#include "garnet/bin/guest/vmm/controller/virtio_rng.h"
#include "garnet/bin/guest/vmm/controller/virtio_wl.h"
#include "garnet/bin/guest/vmm/guest.h"
#include "garnet/bin/guest/vmm/guest_config.h"
#include "garnet/bin/guest/vmm/instance_controller_impl.h"
#include "garnet/bin/guest/vmm/interrupt_controller.h"
#include "garnet/bin/guest/vmm/linux.h"
#include "garnet/bin/guest/vmm/pci.h"
#include "garnet/bin/guest/vmm/platform_device.h"
#include "garnet/bin/guest/vmm/uart.h"
#include "garnet/bin/guest/vmm/vcpu.h"
#include "garnet/bin/guest/vmm/virtio_net_legacy.h"
#include "garnet/bin/guest/vmm/virtio_vsock.h"
#include "garnet/bin/guest/vmm/zircon.h"
#include "src/lib/files/file.h"

#if __aarch64__
#include "garnet/bin/guest/vmm/arch/arm64/pl031.h"

#elif __x86_64__
#include "garnet/bin/guest/vmm/arch/x64/acpi.h"
#include "garnet/bin/guest/vmm/arch/x64/io_port.h"
#include "garnet/bin/guest/vmm/arch/x64/page_table.h"

static constexpr char kDsdtPath[] = "/pkg/data/dsdt.aml";
static constexpr char kMcfgPath[] = "/pkg/data/mcfg.aml";
#endif

// For devices that can have their addresses anywhere we run a dynamic
// allocator that starts fairly high in the guest physical address space.
static constexpr zx_gpaddr_t kFirstDynamicDeviceAddr = 0xc00000000;

static zx_status_t read_guest_cfg(const char* cfg_path, int argc, char** argv,
                                  GuestConfig* cfg) {
  GuestConfigParser parser(cfg);
  std::string cfg_str;
  if (files::ReadFileToString(cfg_path, &cfg_str)) {
    zx_status_t status = parser.ParseConfig(cfg_str);
    if (status != ZX_OK) {
      return status;
    }
  }
  zx_status_t status = parser.ParseArgcArgv(argc, argv);
  if (status != ZX_OK) {
    return status;
  }
  parser.SetDefaults();
  return ZX_OK;
}

static zx_gpaddr_t alloc_device_addr(size_t device_size) {
  static zx_gpaddr_t next_device_addr = kFirstDynamicDeviceAddr;
  zx_gpaddr_t ret = next_device_addr;
  next_device_addr += device_size;
  return ret;
}

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  async::Loop device_loop(&kAsyncLoopConfigNoAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  fuchsia::guest::LaunchInfo launch_info;
  fuchsia::guest::vmm::LaunchInfoProviderSyncPtr launch_info_provider;
  context->ConnectToEnvironmentService(launch_info_provider.NewRequest());
  zx_status_t status = launch_info_provider->GetLaunchInfo(&launch_info);
  // NOTE: This isn't an error yet since only the guest_manager exposes the
  // LaunchInfoProvider service. This will become an error once we invert the
  // dependency between guest_runner and guest_manager.
  if (status != ZX_OK) {
    FXL_LOG(INFO) << "No launch info provided.";
  }

  InstanceControllerImpl instance_controller;
  fuchsia::sys::LauncherPtr launcher;
  context->environment()->GetLauncher(launcher.NewRequest());

  GuestConfig cfg;
  status = read_guest_cfg("/guest/data/guest.cfg", argc, argv, &cfg);
  if (status != ZX_OK) {
    return status;
  }

  DevMem dev_mem;
  for (const MemorySpec& spec : cfg.memory()) {
    // Avoid a collision between static and dynamic address assignment.
    if (spec.base + spec.size > kFirstDynamicDeviceAddr) {
      FXL_LOG(ERROR) << "Requested memory should be less than "
                     << kFirstDynamicDeviceAddr;
      return ZX_ERR_INVALID_ARGS;
    }
    // Add device memory range.
    if (spec.policy == MemoryPolicy::HOST_DEVICE &&
        !dev_mem.AddRange(spec.base, spec.size)) {
      FXL_LOG(ERROR) << "Failed to add device memory at 0x" << std::hex
                     << spec.base;
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
  Uart uart(instance_controller.SerialSocket());
  status = uart.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create UART at " << status;
    return status;
  }
  platform_devices.push_back(&uart);
  // Setup interrupt controller.
  InterruptController interrupt_controller(&guest);
#if __aarch64__
  status = interrupt_controller.Init(cfg.cpus(), cfg.interrupts());
#elif __x86_64__
  status = interrupt_controller.Init();
#endif
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create interrupt controller " << status;
    return status;
  }
  platform_devices.push_back(&interrupt_controller);

#if __aarch64__
  // Setup PL031 RTC.
  Pl031 pl031;
  status = pl031.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PL031 RTC " << status;
    return status;
  }
  platform_devices.push_back(&pl031);
#elif __x86_64__
  // Setup IO ports.
  IoPort io_port;
  status = io_port.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create IO ports " << status;
    return status;
  }
#endif

  // Setup PCI.
  PciBus bus(&guest, &interrupt_controller);
  status = bus.Init(device_loop.dispatcher());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PCI bus " << status;
    return status;
  }
  platform_devices.push_back(&bus);

  // Setup balloon device.
  VirtioBalloon balloon(guest.phys_mem());
  if (cfg.virtio_balloon()) {
    status = bus.Connect(balloon.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      return status;
    }
    status =
        balloon.Start(guest.object(), launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start balloon device " << status;
      return status;
    }
  }

  // Setup block device.
  //
  // We first add the devices specified in the package config file, followed by
  // the devices in the launch_info.
  std::vector<fuchsia::guest::BlockDevice> block_infos;
  for (size_t i = 0; i < cfg.block_devices().size(); i++) {
    const auto& block_spec = cfg.block_devices()[i];
    if (block_spec.path.empty()) {
      FXL_LOG(ERROR) << "Block spec missing path attribute " << status;
      return ZX_ERR_INVALID_ARGS;
    }
    uint32_t flags = ZX_FS_RIGHT_READABLE;
    if (block_spec.mode == fuchsia::guest::BlockMode::READ_WRITE) {
      flags |= ZX_FS_RIGHT_WRITABLE;
    }
    fidl::InterfaceHandle<fuchsia::io::File> file;
    status = fdio_open(block_spec.path.c_str(), flags,
                       file.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to open " << block_spec.path << " " << status;
      return status;
    }
    block_infos.push_back({
        .id = fxl::StringPrintf("block-%zu", i),
        .mode = block_spec.mode,
        .format = block_spec.format,
        .file = std::move(file),
    });
  }
  if (launch_info.block_devices) {
    block_infos.insert(
        block_infos.end(),
        std::make_move_iterator(launch_info.block_devices->begin()),
        std::make_move_iterator(launch_info.block_devices->end()));
  }

  // Create a new VirtioBlock device for each device requested.
  std::vector<std::unique_ptr<VirtioBlock>> block_devices;
  for (auto& block_device : block_infos) {
    auto block =
        std::make_unique<VirtioBlock>(block_device.mode, guest.phys_mem());
    status = bus.Connect(block->pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      return status;
    }
    status = block->Start(guest.object(), std::move(block_device.id),
                          block_device.format, block_device.file.Bind(),
                          launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start block device " << status;
      return status;
    }
    block_devices.push_back(std::move(block));
  }

  // Setup console device.
  VirtioConsole console(guest.phys_mem());
  if (cfg.virtio_console()) {
    status = bus.Connect(console.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      return status;
    }
    status = console.Start(guest.object(), instance_controller.SerialSocket(),
                           launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start console device " << status;
      return status;
    }
  }

  VirtioGpu gpu(guest.phys_mem());
  VirtioInput input(guest.phys_mem());
  if (cfg.virtio_gpu()) {
    // Setup input device.
    status = bus.Connect(input.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      return status;
    }
    fidl::InterfaceHandle<fuchsia::guest::device::ViewListener> view_listener;
    status = input.Start(guest.object(), view_listener.NewRequest(),
                         launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      return status;
    }

    // Setup GPU device.
    status = bus.Connect(gpu.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      return status;
    }
    status = gpu.Start(guest.object(), std::move(view_listener), launcher.get(),
                       device_loop.dispatcher());
    if (status != ZX_OK) {
      return status;
    }
  }

  // Setup net device.
  VirtioNetLegacy legacy_net(guest.phys_mem(), device_loop.dispatcher());
  VirtioNet net(guest.phys_mem());
  if (cfg.virtio_net()) {
    if (cfg.legacy_net()) {
      status = bus.Connect(legacy_net.pci_device(), device_loop.dispatcher());
      if (status != ZX_OK) {
        return status;
      }
      status = legacy_net.Start("/dev/class/ethernet/000");
      if (status != ZX_OK) {
        FXL_LOG(INFO) << "Could not open Ethernet device";
        return status;
      }
    } else {
      status = bus.Connect(net.pci_device(), device_loop.dispatcher(), true);
      if (status != ZX_OK) {
        return status;
      }
      status =
          net.Start(guest.object(), launcher.get(), device_loop.dispatcher());
      if (status != ZX_OK) {
        FXL_LOG(INFO) << "Could not open Ethernet device";
        return status;
      }
    }
  }

  // Setup RNG device.
  VirtioRng rng(guest.phys_mem());
  if (cfg.virtio_rng()) {
    status = bus.Connect(rng.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      return status;
    }
    status =
        rng.Start(guest.object(), launcher.get(), device_loop.dispatcher());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start RNG device" << status;
      return status;
    }
  }

  // Setup vsock device. Vsock uses its own dispatcher as a temporary measure
  // until it is moved out of process.
  async::Loop vsock_loop{&kAsyncLoopConfigNoAttachToThread};
  VirtioVsock vsock(context.get(), guest.phys_mem(), vsock_loop.dispatcher());
  if (cfg.virtio_vsock()) {
    status = bus.Connect(vsock.pci_device(), vsock_loop.dispatcher(), false);
    if (status != ZX_OK) {
      return status;
    }
    status = vsock_loop.StartThread("vsock-handler");
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create vsock async worker " << status;
      return status;
    }
  }

  // Setup wayland device.
  VirtioWl wl(guest.phys_mem());
  if (launch_info.wayland_device) {
    size_t wl_dev_mem_size = launch_info.wayland_device->memory;
    zx_gpaddr_t wl_dev_mem_offset = alloc_device_addr(wl_dev_mem_size);
    if (!dev_mem.AddRange(wl_dev_mem_offset, wl_dev_mem_size)) {
      FXL_LOG(INFO)
          << "Could not reserve device memory range for wayland device";
      return status;
    }
    zx::vmar wl_vmar;
    status = guest.CreateSubVmar(wl_dev_mem_offset, wl_dev_mem_size, &wl_vmar);
    if (status != ZX_OK) {
      FXL_LOG(INFO) << "Could not create VMAR for wayland device";
      return status;
    }
    status = bus.Connect(wl.pci_device(), device_loop.dispatcher(), true);
    if (status != ZX_OK) {
      FXL_LOG(INFO) << "Could not connect wayland device";
      return status;
    }
    status = wl.Start(guest.object(), std::move(wl_vmar),
                      std::move(launch_info.wayland_device->dispatcher),
                      launcher.get(), device_loop.dispatcher(),
                      "/dev/class/gpu/000",
                      "/pkg/data/drivers/libvulkan_intel_linux.so");
    if (status != ZX_OK) {
      FXL_LOG(INFO) << "Could not start wayland device";
      return status;
    }
  }

#if __x86_64__
  status = create_page_table(guest.phys_mem());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create page table " << status;
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
    FXL_LOG(ERROR) << "Failed to create ACPI table " << status;
    return status;
  }
#endif  // __x86_64__

  // Add any trap ranges as device memory.
  for (const IoMapping& mapping : guest.mappings()) {
    if ((mapping.kind() == ZX_GUEST_TRAP_MEM ||
         mapping.kind() == ZX_GUEST_TRAP_BELL) &&
        !dev_mem.AddRange(mapping.base(), mapping.size())) {
      FXL_LOG(ERROR) << "Failed to add trap range as device memory";
      return ZX_ERR_INTERNAL;
    }
  }

  // Setup kernel.
  uintptr_t entry = 0;
  uintptr_t boot_ptr = 0;
  switch (cfg.kernel()) {
    case Kernel::ZIRCON:
      status = setup_zircon(cfg, guest.phys_mem(), dev_mem, platform_devices,
                            &entry, &boot_ptr);
      break;
    case Kernel::LINUX:
      status = setup_linux(cfg, guest.phys_mem(), dev_mem, platform_devices,
                           &entry, &boot_ptr);
      break;
    default:
      FXL_LOG(ERROR) << "Unknown kernel";
      return ZX_ERR_INVALID_ARGS;
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load kernel " << cfg.kernel_path() << " "
                   << status;
    return status;
  }

  // Setup primary VCPU.
  status = guest.StartVcpu(0 /* id */, entry, boot_ptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start VCPU-0 " << status;
    loop.Quit();
  }

  status = instance_controller.AddPublicService(context.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add public service " << status;
    loop.Quit();
  }
  status = balloon.AddPublicService(context.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add public service " << status;
    loop.Quit();
  }

  // Start the dispatch thread for communicating with the out of process
  // devices.
  status = device_loop.StartThread("device-worker");
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create async worker " << status;
    return status;
  }

  loop.Run();
  return guest.Join();
}
