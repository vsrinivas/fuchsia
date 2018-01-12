// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <iostream>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/guest.h>
#include <hypervisor/vcpu.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/bin/guest/guest_view.h"
#include "garnet/bin/guest/linux.h"
#include "garnet/bin/guest/zircon.h"
#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/framebuffer_scanout.h"
#include "garnet/lib/machina/hid_event_source.h"
#include "garnet/lib/machina/input_dispatcher.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/pci.h"
#include "garnet/lib/machina/uart.h"
#include "garnet/lib/machina/virtio_balloon.h"
#include "garnet/lib/machina/virtio_block.h"
#include "garnet/lib/machina/virtio_gpu.h"
#include "garnet/lib/machina/virtio_input.h"

#if __aarch64__
#include "garnet/lib/machina/arch/arm64/pl031.h"

static const size_t kNumUarts = 1;
static const uint64_t kUartBases[kNumUarts] = {
    // TODO(abdulla): Considering parsing this from the MDI.
    machina::kPl011PhysBase,
};
#elif __x86_64__
#include <hypervisor/x86/acpi.h>
#include "garnet/lib/machina/arch/x86/io_port.h"
#include "garnet/lib/machina/arch/x86/page_table.h"
#include "garnet/lib/machina/arch/x86/tpm.h"

static const char kDsdtPath[] = "/pkg/data/dsdt.aml";
static const char kMcfgPath[] = "/pkg/data/mcfg.aml";
static const size_t kNumUarts = 4;
static const uint64_t kUartBases[kNumUarts] = {
    machina::kI8250Base0,
    machina::kI8250Base1,
    machina::kI8250Base2,
    machina::kI8250Base3,
};
#endif

static const uint64_t kVmoSize = 1u << 30;
static const size_t kInputQueueDepth = 64;

// Unused memory above this threshold may be reclaimed by the balloon.
static uint32_t balloon_threshold_pages = 1024;

static zx_status_t usage(const char* cmd) {
  // clang-format off
  std::cerr << "usage: " << cmd << " [OPTIONS] kernel.bin\n";
  std::cerr << "\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "\t-b [block.bin]     Use file 'block.bin' as a virtio-block device\n";
  std::cerr << "\t-r [ramdisk.bin]   Use file 'ramdisk.bin' as a ramdisk\n";
  std::cerr << "\t-c [cmdline]       Use string 'cmdline' as the kernel command line\n";
  std::cerr << "\t-m [seconds]       Poll the virtio-balloon device every 'seconds' seconds\n";
  std::cerr << "\t                   and adjust the balloon size based on the amount of\n";
  std::cerr << "\t                   unused guest memory\n";
  std::cerr << "\t-p [pages]         Number of unused pages to allow the guest to\n";
  std::cerr << "\t                   retain. Has no effect unless -m is also used\n";
  std::cerr << "\t-d                 Demand-page balloon deflate requests\n";
  std::cerr << "\n";
  // clang-format on
  return ZX_ERR_INVALID_ARGS;
}

static void balloon_stats_handler(machina::VirtioBalloon* balloon,
                                  const virtio_balloon_stat_t* stats,
                                  size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (stats[i].tag != VIRTIO_BALLOON_S_AVAIL) {
      continue;
    }

    uint32_t current_pages = balloon->num_pages();
    uint32_t available_pages =
        static_cast<uint32_t>(stats[i].val / machina::VirtioBalloon::kPageSize);
    uint32_t target_pages =
        current_pages + (available_pages - balloon_threshold_pages);
    if (current_pages == target_pages) {
      return;
    }

    FXL_LOG(INFO) << "adjusting target pages " << std::hex << current_pages
                  << " -> " << std::hex << target_pages;
    zx_status_t status = balloon->UpdateNumPages(target_pages);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error " << status << " updating balloon size.";
    }
    return;
  }
}

typedef struct balloon_task_args {
  machina::VirtioBalloon* balloon;
  zx_duration_t interval;
} balloon_task_args_t;

static int balloon_stats_task(void* ctx) {
  fbl::unique_ptr<balloon_task_args_t> args(
      static_cast<balloon_task_args_t*>(ctx));
  machina::VirtioBalloon* balloon = args->balloon;
  while (true) {
    zx_nanosleep(zx_deadline_after(args->interval));
    args->balloon->RequestStats(
        [balloon](const virtio_balloon_stat_t* stats, size_t len) {
          balloon_stats_handler(balloon, stats, len);
        });
  }
  return ZX_OK;
}

static zx_status_t poll_balloon_stats(machina::VirtioBalloon* balloon,
                                      zx_duration_t interval) {
  thrd_t thread;
  auto args = new balloon_task_args_t{balloon, interval};

  int ret = thrd_create(&thread, balloon_stats_task, args);
  if (ret != thrd_success) {
    FXL_LOG(ERROR) << "Failed to create balloon thread " << ret << ".";
    delete args;
    return ZX_ERR_INTERNAL;
  }

  ret = thrd_detach(thread);
  if (ret != thrd_success) {
    FXL_LOG(ERROR) << "Failed to detach balloon thread " << ret << ".";
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

static const char* zircon_cmdline(const char* cmdline) {
  static fbl::StringBuffer<LINE_MAX> buffer;
  buffer.AppendPrintf("TERM=uart %s", cmdline);
  return buffer.c_str();
}

static const char* linux_cmdline(const char* cmdline,
                                 uintptr_t uart_addr,
                                 uintptr_t acpi_addr) {
  static fbl::StringBuffer<LINE_MAX> buffer;
#if __aarch64__
  buffer.AppendPrintf("earlycon=pl011,%#lx console=ttyAMA0 console=tty0 %s",
                      uart_addr, cmdline);
#elif __x86_64__
  buffer.AppendPrintf(
      "earlycon=uart,io,%#lx console=ttyS0 console=tty0 io_delay=none "
      "clocksource=tsc acpi_rsdp=%#lx %s",
      uart_addr, acpi_addr, cmdline);
#endif
  return buffer.c_str();
}

zx_status_t setup_zircon_framebuffer(
    machina::VirtioGpu* gpu,
    fbl::unique_ptr<machina::GpuScanout>* scanout) {
  zx_status_t status = machina::FramebufferScanout::Create(
      "/dev/class/framebuffer/000", scanout);
  if (status != ZX_OK)
    return status;
  return gpu->AddScanout(scanout->get());
}

zx_status_t setup_scenic_framebuffer(
    machina::VirtioGpu* gpu,
    machina::InputDispatcher* input_dispatcher) {
  return GuestView::Start(gpu, input_dispatcher);
}

int main(int argc, char** argv) {
  const char* cmd = basename(argv[0]);
  const char* block_path = nullptr;
  const char* kernel_path = "/pkg/data/kernel";
  const char* ramdisk_path = "/pkg/data/ramdisk";
  const char* cmdline = "";
  zx_duration_t balloon_poll_interval = 0;
  bool balloon_deflate_on_demand = false;
  int opt;
  while ((opt = getopt(argc, argv, "b:r:c:m:dp:")) != -1) {
    switch (opt) {
      case 'b':
        block_path = optarg;
        break;
      case 'r':
        ramdisk_path = optarg;
        break;
      case 'c':
        cmdline = optarg;
        break;
      case 'm':
        balloon_poll_interval = ZX_SEC(strtoul(optarg, nullptr, 10));
        if (balloon_poll_interval <= 0) {
          FXL_LOG(ERROR) << "Invalid balloon interval " << optarg << "."
                         << "Must be an integer greater than 0.";
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case 'd':
        balloon_deflate_on_demand = true;
        break;
      case 'p':
        balloon_threshold_pages =
            static_cast<uint32_t>(strtoul(optarg, nullptr, 10));
        if (balloon_threshold_pages <= 0) {
          FXL_LOG(ERROR) << "Invalid balloon threshold " << optarg << "."
                         << "Must be an integer greater than 0.";
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      default:
        return usage(cmd);
    }
  }
  if (optind < argc) {
    kernel_path = argv[optind];
  }

  Guest guest;
  zx_status_t status = guest.Init(kVmoSize);
  if (status != ZX_OK)
    return status;

  uintptr_t physmem_addr = guest.phys_mem().addr();
  size_t physmem_size = guest.phys_mem().size();
  uintptr_t pt_end_off = 0;

#if __x86_64__
  status = machina::create_page_table(physmem_addr, physmem_size, &pt_end_off);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create page table.";
    return status;
  }

  struct acpi_config acpi_config = {
      .dsdt_path = kDsdtPath,
      .mcfg_path = kMcfgPath,
      .io_apic_addr = machina::kIoApicPhysBase,
      .num_cpus = 1,
  };
  status =
      create_acpi_table(acpi_config, physmem_addr, physmem_size, pt_end_off);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create ACPI table.";
    return status;
  }
#endif  // __x86_64__

  // Open the kernel image.
  fbl::unique_fd fd(open(kernel_path, O_RDONLY));
  if (!fd) {
    FXL_LOG(ERROR) << "Failed to open kernel image \"" << kernel_path << "\".";
    return ZX_ERR_IO;
  }

  // Load the first page to pass to setup functions.
  uintptr_t first_page = physmem_addr + physmem_size - PAGE_SIZE;
  ssize_t ret = read(fd.get(), (void*)first_page, PAGE_SIZE);
  if (ret != PAGE_SIZE) {
    FXL_LOG(ERROR) << "Failed to read first page of kernel.";
    return ZX_ERR_IO;
  }

  uintptr_t guest_ip = 0;
  uintptr_t boot_ptr = 0;
  status =
      setup_zircon(physmem_addr, physmem_size, first_page, pt_end_off, fd.get(),
                   ramdisk_path, zircon_cmdline(cmdline), &guest_ip, &boot_ptr);
  if (status == ZX_ERR_NOT_SUPPORTED) {
    ret = lseek(fd.get(), 0, SEEK_SET);
    if (ret < 0) {
      FXL_LOG(ERROR) << "Failed to seek to start of kernel.";
      return ZX_ERR_IO;
    }
    status = setup_linux(physmem_addr, physmem_size, first_page, fd.get(),
                         ramdisk_path,
                         linux_cmdline(cmdline, kUartBases[0], pt_end_off),
                         &guest_ip, &boot_ptr);
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load kernel.";
    return status;
  }

  zx_vcpu_create_args_t args = {
    guest_ip,
#if __x86_64__
    0 /* cr3 */,
    0 /* apic_vmo */,
#endif  // __x86_64__
  };
  Vcpu vcpu;
  status = vcpu.Create(&guest, &args);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create VCPU.";
    return status;
  }

  // Setup UARTs.
  machina::Uart uart[kNumUarts];
  for (size_t i = 0; i < kNumUarts; i++) {
    status = uart[i].Init(&guest, kUartBases[i]);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create UART at " << std::hex << kUartBases[i]
                     << ".";
      return status;
    }
  }
  // Setup interrupt controller.
  machina::InterruptController interrupt_controller;
  status = interrupt_controller.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create interrupt controller.";
    return status;
  }

#if __aarch64__
  status = interrupt_controller.RegisterVcpu(0, &vcpu);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to register VCPU with GIC distributor.";
    return status;
  }
  machina::Pl031 pl031;
  status = pl031.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PL031 RTC.";
    return status;
  }
#elif __x86_64__
  // Register VCPU with local APIC ID 0.
  status = interrupt_controller.RegisterVcpu(0, &vcpu);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to register VCPU with IO APIC.";
    return status;
  }
  // Setup IO ports.
  machina::IoPort io_port;
  status = io_port.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create IO ports.";
    return status;
  }
  // Setup TPM
  machina::Tpm tpm;
  status = tpm.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create TPM.";
    return status;
  }
#endif

  // Setup PCI.
  machina::PciBus bus(&guest, &interrupt_controller);
  status = bus.Init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PCI bus.";
    return status;
  }

  // Setup balloon device.
  machina::VirtioBalloon balloon(physmem_addr, physmem_size,
                                 guest.phys_mem().vmo());
  balloon.set_deflate_on_demand(balloon_deflate_on_demand);
  status = bus.Connect(&balloon.pci_device(), PCI_DEVICE_VIRTIO_BALLOON);
  if (status != ZX_OK) {
    return status;
  }
  if (balloon_poll_interval > 0) {
    poll_balloon_stats(&balloon, balloon_poll_interval);
  }

  // Setup block device.
  machina::VirtioBlock block(physmem_addr, physmem_size);
  if (block_path != nullptr) {
    status = block.Init(block_path, guest.phys_mem());
    if (status != ZX_OK) {
      return status;
    }
    status = block.Start();
    if (status != ZX_OK) {
      return status;
    }
    status = bus.Connect(&block.pci_device(), PCI_DEVICE_VIRTIO_BLOCK);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Setup input device.
  machina::InputDispatcher input_dispatcher(kInputQueueDepth);
  machina::HidEventSource hid_event_source(&input_dispatcher);
  machina::VirtioInput input(&input_dispatcher, physmem_addr, physmem_size,
                             "machina-input", "serial-number");
  status = input.Start();
  if (status != ZX_OK) {
    return status;
  }
  status = bus.Connect(&input.pci_device(), PCI_DEVICE_VIRTIO_INPUT);
  if (status != ZX_OK) {
    return status;
  }

  // Setup GPU device.
  machina::VirtioGpu gpu(physmem_addr, physmem_size);
  fbl::unique_ptr<machina::GpuScanout> gpu_scanout;
  status = setup_zircon_framebuffer(&gpu, &gpu_scanout);
  if (status == ZX_OK) {
    // If we were able to acquire the zircon framebuffer then no compositor
    // is present. In this case we should read input events directly from the
    // input devics.
    status = hid_event_source.Start();
    if (status != ZX_OK) {
      return status;
    }
  } else {
    // Expose a view that can be composited by mozart. Input events will be
    // injected by the view events.
    status = setup_scenic_framebuffer(&gpu, &input_dispatcher);
    if (status != ZX_OK) {
      return status;
    }
  }
  status = gpu.Init();
  if (status != ZX_OK) {
    return status;
  }
  status = bus.Connect(&gpu.pci_device(), PCI_DEVICE_VIRTIO_GPU);
  if (status != ZX_OK) {
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
  status = vcpu.Start(&vcpu_state);
  if (status != ZX_OK) {
    return status;
  }

  return vcpu.Join();
}
