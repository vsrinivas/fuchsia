// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ios>
#include <vector>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <trace-provider/provider.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/bin/guest/vmm/guest_config.h"
#include "garnet/bin/guest/vmm/guest_view.h"
#include "garnet/bin/guest/vmm/linux.h"
#include "garnet/bin/guest/vmm/zircon.h"
#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/framebuffer_scanout.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/guest_controller_impl.h"
#include "garnet/lib/machina/hid_event_source.h"
#include "garnet/lib/machina/input_dispatcher.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/pci.h"
#include "garnet/lib/machina/uart.h"
#include "garnet/lib/machina/vcpu.h"
#include "garnet/lib/machina/virtio_balloon.h"
#include "garnet/lib/machina/virtio_block.h"
#include "garnet/lib/machina/virtio_console.h"
#include "garnet/lib/machina/virtio_gpu.h"
#include "garnet/lib/machina/virtio_input.h"
#include "garnet/lib/machina/virtio_net.h"
#include "garnet/lib/machina/virtio_vsock.h"
#include "garnet/public/lib/fxl/files/file.h"
#include "lib/component/cpp/startup_context.h"

#if __aarch64__
#include "garnet/lib/machina/arch/arm64/pl031.h"

static constexpr size_t kNumUarts = 1;
static constexpr uint64_t kUartBases[kNumUarts] = {
    // TODO(abdulla): Considering parsing this from the MDI.
    machina::kPl011PhysBase,
};
#elif __x86_64__
#include "garnet/lib/machina/arch/x86/acpi.h"
#include "garnet/lib/machina/arch/x86/io_port.h"
#include "garnet/lib/machina/arch/x86/page_table.h"
#include "garnet/lib/machina/arch/x86/tpm.h"

static constexpr char kDsdtPath[] = "/pkg/data/dsdt.aml";
static constexpr char kMcfgPath[] = "/pkg/data/mcfg.aml";
static constexpr size_t kNumUarts = 4;
static constexpr uint64_t kUartBases[kNumUarts] = {
    machina::kI8250Base0,
    machina::kI8250Base1,
    machina::kI8250Base2,
    machina::kI8250Base3,
};
#endif

static constexpr size_t kInputQueueDepth = 64;

static void balloon_stats_handler(machina::VirtioBalloon* balloon,
                                  uint32_t threshold,
                                  const virtio_balloon_stat_t* stats,
                                  size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (stats[i].tag != VIRTIO_BALLOON_S_AVAIL) {
      continue;
    }

    uint32_t current_pages = balloon->num_pages();
    uint32_t available_pages =
        static_cast<uint32_t>(stats[i].val / machina::VirtioBalloon::kPageSize);
    uint32_t target_pages = current_pages + (available_pages - threshold);
    if (current_pages == target_pages) {
      return;
    }

    FXL_LOG(INFO) << "adjusting target pages " << std::hex << current_pages
                  << " -> " << std::hex << target_pages;
    zx_status_t status = balloon->UpdateNumPages(target_pages);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error " << status << " updating balloon size";
    }
    return;
  }
}

typedef struct balloon_task_args {
  machina::VirtioBalloon* balloon;
  const GuestConfig* cfg;
} balloon_task_args_t;

static int balloon_stats_task(void* ctx) {
  fbl::unique_ptr<balloon_task_args_t> args(
      static_cast<balloon_task_args_t*>(ctx));
  machina::VirtioBalloon* balloon = args->balloon;
  zx_duration_t interval = args->cfg->balloon_interval();
  uint32_t threshold = args->cfg->balloon_pages_threshold();
  while (true) {
    zx_nanosleep(zx_deadline_after(interval));
    args->balloon->RequestStats(
        [balloon, threshold](const virtio_balloon_stat_t* stats, size_t len) {
          balloon_stats_handler(balloon, threshold, stats, len);
        });
  }
  return ZX_OK;
}

static zx_status_t poll_balloon_stats(machina::VirtioBalloon* balloon,
                                      const GuestConfig* cfg) {
  thrd_t thread;
  auto args = new balloon_task_args_t{balloon, cfg};

  int ret = thrd_create_with_name(&thread, balloon_stats_task, args,
                                  "virtio-balloon");
  if (ret != thrd_success) {
    FXL_LOG(ERROR) << "Failed to create balloon thread " << ret;
    delete args;
    return ZX_ERR_INTERNAL;
  }

  ret = thrd_detach(thread);
  if (ret != thrd_success) {
    FXL_LOG(ERROR) << "Failed to detach balloon thread " << ret;
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

static zx_status_t setup_zircon_framebuffer(
    machina::VirtioGpu* gpu, fbl::unique_ptr<machina::GpuScanout>* scanout) {
  // Try software framebuffer.
  zx_status_t status = machina::FramebufferScanout::Create(scanout);
  if (status != ZX_OK) {
    return status;
  }
  return gpu->AddScanout(scanout->get());
}

static zx_status_t setup_scenic_framebuffer(
    component::StartupContext* startup_context, machina::VirtioGpu* gpu,
    machina::InputDispatcher* input_dispatcher,
    machina::GuestControllerImpl* guest_controller,
    fbl::unique_ptr<machina::GpuScanout>* scanout) {
  fbl::unique_ptr<ScenicScanout> scenic_scanout;
  zx_status_t status =
      ScenicScanout::Create(startup_context, input_dispatcher, &scenic_scanout);
  if (status != ZX_OK) {
    return status;
  }
  guest_controller->set_view_provider(scenic_scanout.get());
  *scanout = std::move(scenic_scanout);
  return gpu->AddScanout(scanout->get());
}

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

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();

  GuestConfig cfg;
  zx_status_t status =
      read_guest_cfg("/guest/data/guest.cfg", argc, argv, &cfg);
  if (status != ZX_OK) {
    return status;
  }

  machina::Guest guest;
  status = guest.Init(cfg.memory());
  if (status != ZX_OK) {
    return status;
  }

  // Instantiate the controller service.
  machina::GuestControllerImpl guest_controller(startup_context.get(),
                                                guest.phys_mem());

#if __x86_64__
  status = machina::create_page_table(guest.phys_mem());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create page table";
    return status;
  }

  machina::AcpiConfig acpi_cfg = {
      .dsdt_path = kDsdtPath,
      .mcfg_path = kMcfgPath,
      .io_apic_addr = machina::kIoApicPhysBase,
      .num_cpus = cfg.num_cpus(),
  };
  status = machina::create_acpi_table(acpi_cfg, guest.phys_mem());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create ACPI table";
    return status;
  }
#endif  // __x86_64__

  // Setup kernel.
  uintptr_t guest_ip = 0;
  uintptr_t boot_ptr = 0;
  switch (cfg.kernel()) {
    case Kernel::ZIRCON:
      status = setup_zircon(cfg, guest.phys_mem(), &guest_ip, &boot_ptr);
      break;
    case Kernel::LINUX:
      status = setup_linux(cfg, guest.phys_mem(), &guest_ip, &boot_ptr);
      break;
    default:
      FXL_LOG(ERROR) << "Unknown kernel";
      return ZX_ERR_INVALID_ARGS;
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load kernel " << cfg.kernel_path();
    return status;
  }

  // Setup UARTs.
  machina::Uart uart[kNumUarts];
  for (size_t i = 0; i < kNumUarts; i++) {
    status = uart[i].Init(&guest, kUartBases[i]);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create UART at " << std::hex
                     << kUartBases[i];
      return status;
    }
  }
  // Setup interrupt controller.
  machina::InterruptController interrupt_controller;
#if __aarch64__
  status = interrupt_controller.Init(&guest, cfg.gic_version());
#elif __x86_64__
  status = interrupt_controller.Init(&guest);
#endif
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create interrupt controller";
    return status;
  }

  auto initialize_vcpu = [boot_ptr, &interrupt_controller](
                             machina::Guest* guest, uintptr_t guest_ip,
                             uint64_t id, machina::Vcpu* vcpu) {
    zx_status_t status = vcpu->Create(guest, guest_ip, id);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create VCPU";
      return status;
    }
    // Register VCPU with ID 0.
    status = interrupt_controller.RegisterVcpu(id, vcpu);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to register VCPU with interrupt controller";
      return status;
    }
    // Setup initial VCPU state.
    zx_vcpu_state_t vcpu_state = {};
#if __aarch64__
    vcpu_state.x[0] = boot_ptr;
#elif __x86_64__
    vcpu_state.rsi = boot_ptr;
#endif
    // Begin VCPU execution.
    return vcpu->Start(&vcpu_state);
  };

  guest.RegisterVcpuFactory(initialize_vcpu);

#if __aarch64__
  machina::Pl031 pl031;
  status = pl031.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PL031 RTC";
    return status;
  }
#elif __x86_64__
  // Setup IO ports.
  machina::IoPort io_port;
  status = io_port.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create IO ports";
    return status;
  }
  // Setup TPM
  machina::Tpm tpm;
  status = tpm.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create TPM";
    return status;
  }
#endif

  // Setup PCI.
  machina::PciBus bus(&guest, &interrupt_controller);
  status = bus.Init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PCI bus";
    return status;
  }

  // Setup balloon device.
  machina::VirtioBalloon balloon(guest.phys_mem());
  balloon.set_deflate_on_demand(cfg.balloon_demand_page());
  status = bus.Connect(balloon.pci_device());
  if (status != ZX_OK) {
    return status;
  }
  if (cfg.balloon_interval() > 0) {
    poll_balloon_stats(&balloon, &cfg);
  }

  // Setup block device.
  std::vector<fbl::unique_ptr<machina::VirtioBlock>> block_devices;
  for (const auto& block_spec : cfg.block_devices()) {
    fbl::unique_ptr<machina::BlockDispatcher> dispatcher;
    if (!block_spec.path.empty()) {
      status = machina::BlockDispatcher::CreateFromPath(
          block_spec.path.c_str(), block_spec.mode, block_spec.data_plane,
          guest.phys_mem(), &dispatcher);
    } else if (!block_spec.guid.empty()) {
      status = machina::BlockDispatcher::CreateFromGuid(
          block_spec.guid, cfg.block_wait() ? ZX_TIME_INFINITE : 0,
          block_spec.mode, block_spec.data_plane, guest.phys_mem(),
          &dispatcher);
    } else {
      FXL_LOG(ERROR) << "Block spec missing path or GUID attributes";
      return ZX_ERR_INVALID_ARGS;
    }
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create block dispatcher " << status;
      return status;
    }
    if (block_spec.volatile_writes) {
      status = machina::BlockDispatcher::CreateVolatileWrapper(
          std::move(dispatcher), &dispatcher);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to create volatile block dispatcher";
        return status;
      }
    }

    auto block = fbl::make_unique<machina::VirtioBlock>(guest.phys_mem());
    status = block->SetDispatcher(std::move(dispatcher));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to set block dispatcher " << status;
      return status;
    }
    status = block->Start();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start block device " << status;
      return status;
    }
    status = bus.Connect(block->pci_device());
    if (status != ZX_OK) {
      return status;
    }
    block_devices.push_back(std::move(block));
  }

  // Setup console
  machina::VirtioConsole console(guest.phys_mem(), guest.device_dispatcher(),
                                 guest_controller.TakeSocket());
  status = console.Start();
  if (status != ZX_OK) {
    return status;
  }
  status = bus.Connect(console.pci_device());
  if (status != ZX_OK) {
    return status;
  }

  machina::InputDispatcher input_dispatcher(kInputQueueDepth);
  machina::HidEventSource hid_event_source(&input_dispatcher);
  machina::VirtioKeyboard keyboard(input_dispatcher.Keyboard(),
                                   guest.phys_mem(), "machina-keyboard",
                                   "serial-number");
  machina::VirtioRelativePointer mouse(input_dispatcher.Mouse(),
                                       guest.phys_mem(), "machina-mouse",
                                       "serial-number");
  machina::VirtioAbsolutePointer touch(
      input_dispatcher.Touch(), guest.phys_mem(), "machina-touch",
      "serial-number", kGuestViewDisplayWidth, kGuestViewDisplayHeight);
  machina::VirtioGpu gpu(guest.phys_mem(), guest.device_dispatcher());
  fbl::unique_ptr<machina::GpuScanout> gpu_scanout;

  if (cfg.display() != GuestDisplay::NONE) {
    // Setup keyboard device.
    status = keyboard.Start();
    if (status != ZX_OK) {
      return status;
    }
    status = bus.Connect(keyboard.pci_device());
    if (status != ZX_OK) {
      return status;
    }

    // Setup mouse device.
    status = mouse.Start();
    if (status != ZX_OK) {
      return status;
    }
    status = bus.Connect(mouse.pci_device());
    if (status != ZX_OK) {
      return status;
    }

    // Setup touch device. Note that this device is used for all pointer events
    // when using a scenic framebuffer because the pointer positions are
    // absolute even when using a mouse.
    status = touch.Start();
    if (status != ZX_OK) {
      return status;
    }
    status = bus.Connect(touch.pci_device());
    if (status != ZX_OK) {
      return status;
    }

    if (cfg.display() == GuestDisplay::FRAMEBUFFER) {
      // Setup GPU device.
      status = setup_zircon_framebuffer(&gpu, &gpu_scanout);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to acquire framebuffer " << status;
        return status;
      }
      // When displaying to the framebuffer, we should read input events
      // directly from the input devics.
      status = hid_event_source.Start();
    } else {
      // Expose a view that can be composited by mozart. Input events will be
      // injected by the view events.
      status = setup_scenic_framebuffer(startup_context.get(), &gpu,
                                        &input_dispatcher, &guest_controller,
                                        &gpu_scanout);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to create scenic view " << status;
        return status;
      }
    }
    if (status == ZX_OK) {
      status = gpu.Init();
      if (status != ZX_OK) {
        return status;
      }
      status = bus.Connect(gpu.pci_device());
      if (status != ZX_OK) {
        return status;
      }
    }
  }

  // Setup net device.
  machina::VirtioNet net(guest.phys_mem(), guest.device_dispatcher());
  if (cfg.network()) {
    status = net.Start("/dev/class/ethernet/000");
    if (status == ZX_OK) {
      // If we started the net device, then connect to the PCI bus.
      status = bus.Connect(net.pci_device());
      if (status != ZX_OK) {
        return status;
      }
    } else {
      FXL_LOG(INFO) << "Could not open Ethernet device";
    }
  }

  // Setup vsock device.
  machina::VirtioVsock vsock(startup_context.get(), guest.phys_mem(),
                             guest.device_dispatcher());
  status = bus.Connect(vsock.pci_device());
  if (status != ZX_OK) {
    return status;
  }

  // GPU back-ends can take some time to initialize. Wait for them to be
  // created before starting the VCPU so that we can ensure we have the
  // framebuffer allocated before software attempts to interface with it.
  auto start_task = [&loop, &guest, guest_ip] {
    zx_status_t status = guest.StartVcpu(guest_ip, 0 /* id */);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start VCPU-0 " << status;
      loop.Quit();
    }
  };
  if (gpu_scanout) {
    gpu_scanout->WhenReady([&loop, &start_task] {
      async::PostTask(loop.dispatcher(), start_task);
    });

  } else {
    start_task();
  }

  loop.Run();
  return guest.Join();
}
