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

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/guest.h>
#include <hypervisor/vcpu.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/bin/guest/guest_config.h"
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
#include "garnet/lib/machina/virtio_net.h"
#include "lib/fxl/files/file.h"

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
      FXL_LOG(ERROR) << "Error " << status << " updating balloon size.";
    }
    return;
  }
}

typedef struct balloon_task_args {
  machina::VirtioBalloon* balloon;
  const GuestConfig* config;
} balloon_task_args_t;

static int balloon_stats_task(void* ctx) {
  fbl::unique_ptr<balloon_task_args_t> args(
      static_cast<balloon_task_args_t*>(ctx));
  machina::VirtioBalloon* balloon = args->balloon;
  zx_duration_t interval = args->config->balloon_interval();
  uint32_t threshold = args->config->balloon_pages_threshold();
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
                                      const GuestConfig* config) {
  thrd_t thread;
  auto args = new balloon_task_args_t{balloon, config};

  int ret = thrd_create_with_name(&thread, balloon_stats_task, args,
                                  "virtio-balloon");
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

static const char* linux_cmdline(const char* cmdline, uintptr_t acpi_addr) {
#if __aarch64__
  return cmdline;
#elif __x86_64__
  static fbl::StringBuffer<LINE_MAX> buffer;
  buffer.AppendPrintf("acpi_rsdp=%#lx %s", acpi_addr, cmdline);
  return buffer.c_str();
#endif
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

zx_status_t read_guest_config(GuestConfig* options,
                              const char* config_path,
                              int argc,
                              char** argv) {
  GuestConfigParser parser(options);
  std::string config;
  if (files::ReadFileToString(config_path, &config)) {
    zx_status_t status = parser.ParseConfig(config);
    if (status != ZX_OK) {
      return status;
    }
  }
  return parser.ParseArgcArgv(argc, argv);
}

int main(int argc, char** argv) {
  GuestConfig options;
  zx_status_t status =
      read_guest_config(&options, "/pkg/data/guest.cfg", argc, argv);
  if (status != ZX_OK) {
    return status;
  }

  Guest guest;
  status = guest.Init(kVmoSize);
  if (status != ZX_OK)
    return status;

  uintptr_t physmem_addr = guest.phys_mem().addr();
  size_t physmem_size = guest.phys_mem().size();
  uintptr_t pt_end_off = 0;

#if __x86_64__
  status = machina::create_page_table(guest.phys_mem(), &pt_end_off);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create page table";
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
    FXL_LOG(ERROR) << "Failed to create ACPI table";
    return status;
  }
#endif  // __x86_64__

  // Open the kernel image.
  fbl::unique_fd fd(open(options.kernel_path().c_str(), O_RDONLY));
  if (!fd) {
    FXL_LOG(ERROR) << "Failed to open kernel image \"" << options.kernel_path();
    return ZX_ERR_IO;
  }

  // Load the first page to pass to setup functions.
  uintptr_t first_page = physmem_addr + physmem_size - PAGE_SIZE;
  ssize_t ret = read(fd.get(), (void*)first_page, PAGE_SIZE);
  if (ret != PAGE_SIZE) {
    FXL_LOG(ERROR) << "Failed to read first page of kernel";
    return ZX_ERR_IO;
  }

  uintptr_t guest_ip = 0;
  uintptr_t boot_ptr = 0;
  status = setup_zircon(physmem_addr, physmem_size, first_page, pt_end_off,
                        fd.get(), options.ramdisk_path().c_str(),
                        options.cmdline().c_str(), &guest_ip, &boot_ptr);
  if (status == ZX_ERR_NOT_SUPPORTED) {
    ret = lseek(fd.get(), 0, SEEK_SET);
    if (ret < 0) {
      FXL_LOG(ERROR) << "Failed to seek to start of kernel";
      return ZX_ERR_IO;
    }
    status = setup_linux(physmem_addr, physmem_size, first_page, fd.get(),
                         options.ramdisk_path().c_str(),
                         linux_cmdline(options.cmdline().c_str(), pt_end_off),
                         &guest_ip, &boot_ptr);
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load kernel";
    return status;
  }

  zx_vcpu_create_args_t args = {
    guest_ip,
#if __x86_64__
    0 /* cr3 */,
#endif  // __x86_64__
  };
  Vcpu vcpu;
  status = vcpu.Create(&guest, &args);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create VCPU";
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
  status = interrupt_controller.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create interrupt controller";
    return status;
  }

#if __aarch64__
  status = interrupt_controller.RegisterVcpu(0, &vcpu);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to register VCPU with GIC distributor";
    return status;
  }
  machina::Pl031 pl031;
  status = pl031.Init(&guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PL031 RTC";
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
  balloon.set_deflate_on_demand(options.balloon_demand_page());
  status = bus.Connect(balloon.pci_device(), PCI_DEVICE_VIRTIO_BALLOON);
  if (status != ZX_OK) {
    return status;
  }
  if (options.balloon_interval() > 0) {
    poll_balloon_stats(&balloon, &options);
  }

  // Setup block device.
  machina::VirtioBlock block(guest.phys_mem());
  if (!options.block_devices().empty()) {
    if (options.block_devices().size() > 1) {
      FXL_LOG(ERROR) << "Multiple block devices are not yet supported";
      return ZX_ERR_NOT_SUPPORTED;
    }
    const BlockSpec& block_spec = options.block_devices()[0];
    status = block.Init(block_spec.path.c_str());
    if (status != ZX_OK) {
      return status;
    }
    status = block.Start();
    if (status != ZX_OK) {
      return status;
    }
    status = bus.Connect(block.pci_device(), PCI_DEVICE_VIRTIO_BLOCK);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Setup input device.
  machina::InputDispatcher input_dispatcher(kInputQueueDepth);
  machina::HidEventSource hid_event_source(&input_dispatcher);
  machina::VirtioInput input(&input_dispatcher, guest.phys_mem(),
                             "machina-input", "serial-number");
  status = input.Start();
  if (status != ZX_OK) {
    return status;
  }
  status = bus.Connect(input.pci_device(), PCI_DEVICE_VIRTIO_INPUT);
  if (status != ZX_OK) {
    return status;
  }

  // Setup GPU device.
  machina::VirtioGpu gpu(guest.phys_mem());
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
  status = bus.Connect(gpu.pci_device(), PCI_DEVICE_VIRTIO_GPU);
  if (status != ZX_OK) {
    return status;
  }

  // Setup net device.
  machina::VirtioNet net(guest.phys_mem());
  status = net.Start();
  if (status != ZX_OK) {
    return status;
  }
  status = bus.Connect(net.pci_device(), PCI_DEVICE_VIRTIO_NET);
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
