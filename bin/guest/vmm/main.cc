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
#include <lib/fdio/util.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/fzl/fdio.h>
#include <trace-provider/provider.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/bin/guest/vmm/guest_config.h"
#include "garnet/bin/guest/vmm/instance_controller_impl.h"
#include "garnet/bin/guest/vmm/linux.h"
#include "garnet/bin/guest/vmm/wayland_dispatcher_impl.h"
#include "garnet/bin/guest/vmm/zircon.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/pci.h"
#include "garnet/lib/machina/platform_device.h"
#include "garnet/lib/machina/uart.h"
#include "garnet/lib/machina/vcpu.h"
#include "garnet/lib/machina/virtio_balloon.h"
#include "garnet/lib/machina/virtio_block.h"
#include "garnet/lib/machina/virtio_console.h"
#include "garnet/lib/machina/virtio_gpu.h"
#include "garnet/lib/machina/virtio_input.h"
#include "garnet/lib/machina/virtio_net.h"
#include "garnet/lib/machina/virtio_net_legacy.h"
#include "garnet/lib/machina/virtio_rng.h"
#include "garnet/lib/machina/virtio_vsock.h"
#include "garnet/lib/machina/virtio_wl.h"
#include "garnet/public/lib/fxl/files/file.h"

#if __aarch64__
#include "garnet/lib/machina/arch/arm64/pl031.h"

#elif __x86_64__
#include "garnet/lib/machina/arch/x86/acpi.h"
#include "garnet/lib/machina/arch/x86/io_port.h"
#include "garnet/lib/machina/arch/x86/page_table.h"

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
  return parser.ParseArgcArgv(argc, argv);
}

static zx_gpaddr_t alloc_device_addr(size_t device_size) {
  static zx_gpaddr_t next_device_addr = kFirstDynamicDeviceAddr;
  zx_gpaddr_t ret = next_device_addr;
  next_device_addr += device_size;
  return ret;
}

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  fuchsia::guest::LaunchInfo launch_info;
  fuchsia::guest::vmm::LaunchInfoProviderSyncPtr launch_info_provider;
  context->ConnectToEnvironmentService(launch_info_provider.NewRequest());
  zx_status_t status = launch_info_provider->GetLaunchInfo(&launch_info);
  // This isn't an error yet since only the guestmgr exposes the
  // LaunchInfoProvider service. This will become an error once we invert the
  // dependency between guest_runner and guestmgr.
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

  // Having memory overlap with dynamic device assignment will work, as any
  // devices will get subtracted from the RAM list later. But it will probably
  // result in much less RAM than expected and so we shall consider it an error.
  if (cfg.memory() >= kFirstDynamicDeviceAddr) {
    FXL_LOG(ERROR) << "Requested memory should be less than "
                   << kFirstDynamicDeviceAddr;
    return ZX_ERR_INVALID_ARGS;
  }

  machina::Guest guest;
  status = guest.Init(cfg.memory(), cfg.host_memory());
  if (status != ZX_OK) {
    return status;
  }

  std::vector<machina::PlatformDevice*> platform_devices;

  // Setup UARTs.
  machina::Uart uart(instance_controller.SerialSocket());
  status = uart.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create UART at " << status;
    return status;
  }
  platform_devices.push_back(&uart);
  // Setup interrupt controller.
  machina::InterruptController interrupt_controller(&guest);
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
  machina::Pl031 pl031;
  status = pl031.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PL031 RTC " << status;
    return status;
  }
  platform_devices.push_back(&pl031);
#elif __x86_64__
  // Setup IO ports.
  machina::IoPort io_port;
  status = io_port.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create IO ports " << status;
    return status;
  }
#endif

  // Setup PCI.
  machina::PciBus bus(&guest, &interrupt_controller);
  status = bus.Init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PCI bus " << status;
    return status;
  }
  platform_devices.push_back(&bus);

  // Setup balloon device.
  machina::VirtioBalloon balloon(guest.phys_mem());
  if (cfg.virtio_balloon()) {
    status = bus.Connect(balloon.pci_device(), true);
    if (status != ZX_OK) {
      return status;
    }
    status = balloon.Start(*guest.object(), launcher.get(),
                           guest.device_dispatcher());
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
  std::vector<std::unique_ptr<machina::VirtioBlock>> block_devices;
  for (auto& block_device : block_infos) {
    auto block = std::make_unique<machina::VirtioBlock>(block_device.mode,
                                                        guest.phys_mem());
    status = bus.Connect(block->pci_device(), true);
    if (status != ZX_OK) {
      return status;
    }
    status = block->Start(*guest.object(), std::move(block_device.id),
                          block_device.format, block_device.file.Bind(),
                          launcher.get(), guest.device_dispatcher());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start block device " << status;
      return status;
    }
    block_devices.push_back(std::move(block));
  }

  // Setup console device.
  machina::VirtioConsole console(guest.phys_mem());
  if (cfg.virtio_console()) {
    status = bus.Connect(console.pci_device(), true);
    if (status != ZX_OK) {
      return status;
    }
    status = console.Start(*guest.object(), instance_controller.SerialSocket(),
                           launcher.get(), guest.device_dispatcher());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start console device " << status;
      return status;
    }
  }

  machina::VirtioGpu gpu(guest.phys_mem());
  machina::VirtioInput input(guest.phys_mem());
  if (cfg.virtio_gpu()) {
    // Setup input device.
    status = bus.Connect(input.pci_device(), true);
    if (status != ZX_OK) {
      return status;
    }
    fidl::InterfaceHandle<fuchsia::ui::input::InputListener> input_listener;
    fidl::InterfaceHandle<fuchsia::guest::device::ViewListener> view_listener;
    status = input.Start(*guest.object(), input_listener.NewRequest(),
                         view_listener.NewRequest(), launcher.get(),
                         guest.device_dispatcher());
    if (status != ZX_OK) {
      return status;
    }

    // Setup GPU device.
    status = bus.Connect(gpu.pci_device(), true);
    if (status != ZX_OK) {
      return status;
    }
    status = gpu.Start(*guest.object(), std::move(input_listener),
                       std::move(view_listener), launcher.get(),
                       guest.device_dispatcher());
    if (status != ZX_OK) {
      return status;
    }
  }

  // Setup net device.
  machina::VirtioNetLegacy legacy_net(guest.phys_mem(),
                                      guest.device_dispatcher());
  machina::VirtioNet net(guest.phys_mem());
  if (cfg.virtio_net()) {
    if (cfg.legacy_net()) {
      status = bus.Connect(legacy_net.pci_device());
      if (status != ZX_OK) {
        return status;
      }
      status = legacy_net.Start("/dev/class/ethernet/000");
      if (status != ZX_OK) {
        FXL_LOG(INFO) << "Could not open Ethernet device";
        return status;
      }
    } else {
      status = bus.Connect(net.pci_device(), true);
      if (status != ZX_OK) {
        return status;
      }
      status =
          net.Start(*guest.object(), launcher.get(), guest.device_dispatcher());
      if (status != ZX_OK) {
        FXL_LOG(INFO) << "Could not open Ethernet device";
        return status;
      }
    }
  }

  // Setup RNG device.
  machina::VirtioRng rng(guest.phys_mem());
  if (cfg.virtio_rng()) {
    status = bus.Connect(rng.pci_device(), true);
    if (status != ZX_OK) {
      return status;
    }
    status =
        rng.Start(*guest.object(), launcher.get(), guest.device_dispatcher());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start RNG device" << status;
      return status;
    }
  }

  // Setup vsock device.
  machina::VirtioVsock vsock(context.get(), guest.phys_mem(),
                             guest.device_dispatcher());
  if (cfg.virtio_vsock()) {
    status = bus.Connect(vsock.pci_device());
    if (status != ZX_OK) {
      return status;
    }
  }

  machina::DevMem dev_mem;

  // Setup wayland device.
  size_t wl_dev_mem_size = cfg.wl_memory();
  zx_gpaddr_t wl_dev_mem_offset = alloc_device_addr(wl_dev_mem_size);
  if (!dev_mem.AddRange(wl_dev_mem_offset, wl_dev_mem_size)) {
    FXL_LOG(INFO) << "Could not reserve device memory range for wayland device";
    return status;
  }
  zx::vmar wl_vmar;
  status = guest.CreateSubVmar(wl_dev_mem_offset, wl_dev_mem_size, &wl_vmar);
  if (status != ZX_OK) {
    FXL_LOG(INFO) << "Could not create VMAR for wayland device";
    return status;
  }
  WaylandDispatcherImpl wl_dispatcher(launcher.get());
  machina::VirtioWl wl(guest.phys_mem());
  if (cfg.virtio_wl()) {
    status = bus.Connect(wl.pci_device(), true);
    if (status != ZX_OK) {
      FXL_LOG(INFO) << "Could not connect wayland device";
      return status;
    }
    status = wl.Start(*guest.object(), std::move(wl_vmar),
                      wl_dispatcher.NewBinding(), launcher.get(),
                      guest.device_dispatcher());
    if (status != ZX_OK) {
      FXL_LOG(INFO) << "Could not start wayland device";
      return status;
    }
  }

#if __x86_64__
  status = machina::create_page_table(guest.phys_mem());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create page table " << status;
    return status;
  }

  machina::AcpiConfig acpi_cfg = {
      .dsdt_path = kDsdtPath,
      .mcfg_path = kMcfgPath,
      .io_apic_addr = machina::IoApic::kPhysBase,
      .cpus = cfg.cpus(),
  };
  status = machina::create_acpi_table(acpi_cfg, guest.phys_mem());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create ACPI table " << status;
    return status;
  }
#endif  // __x86_64__

  // Add any trap ranges as device memory.
  for (const machina::IoMapping& mapping : guest.mappings()) {
    if (mapping.kind() == ZX_GUEST_TRAP_MEM ||
        mapping.kind() == ZX_GUEST_TRAP_BELL) {
      if (!dev_mem.AddRange(mapping.base(), mapping.size())) {
        FXL_LOG(ERROR) << "Failed to add trap range as device memory";
        return ZX_ERR_INTERNAL;
      }
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

  loop.Run();
  return guest.Join();
}
