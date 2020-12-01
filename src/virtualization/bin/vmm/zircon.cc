// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/zircon.h"

#include <fcntl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/zbitl/error_string.h>
#include <lib/zbitl/fd.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/memory.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/image.h>

#include <iterator>

#include <fbl/unique_fd.h>

#include "src/virtualization/bin/vmm/dev_mem.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/zbi.h"

#if __aarch64__
// This address works for direct-mapping of host memory. This address is chosen
// to ensure that we do not collide with the mapping of the host kernel.
static constexpr uintptr_t kKernelOffset = 0x2080000;

static constexpr zbi_platform_id_t kPlatformId = {
    .vid = 3,  // PDEV_VID_GOOGLE
    .pid = 2,  // PDEV_PID_MACHINA
    .board_name = "machina",
};

static constexpr dcfg_arm_psci_driver_t kPsciDriver = {
    .use_hvc = false,
};

static constexpr dcfg_arm_generic_timer_driver_t kTimerDriver = {
    .irq_virt = 27,
};
#elif __x86_64__
static constexpr uintptr_t kKernelOffset = 0x100000;

#include "src/virtualization/bin/vmm/arch/x64/acpi.h"
#include "src/virtualization/bin/vmm/arch/x64/e820.h"
#endif

static constexpr uintptr_t kRamdiskOffset = 0x4000000;

static inline bool is_within(uintptr_t x, uintptr_t addr, uintptr_t size) {
  return x >= addr && x < addr + size;
}

zx_status_t read_unified_zbi(fbl::unique_fd zbi_fd, const uintptr_t kernel_zbi_off,
                             const uintptr_t data_zbi_off, const PhysMem& phys_mem,
                             uintptr_t* guest_ip) {
  // Alias for clarity.
  using complete_zbi_t = zircon_kernel_t;

  if (ZBI_ALIGN(kernel_zbi_off) != kernel_zbi_off) {
    FX_LOGS(ERROR) << "Kernel ZBI offset has invalid alignment";
    return ZX_ERR_INVALID_ARGS;
  }
  if (ZBI_ALIGN(data_zbi_off) != data_zbi_off) {
    FX_LOGS(ERROR) << "Data ZBI offset has invalid alignment";
    return ZX_ERR_INVALID_ARGS;
  }

  if (!zbi_fd) {
    FX_LOGS(ERROR) << "Failed to open ZBI";
    return ZX_ERR_IO;
  }

  // Read out the initial headers to check that the ZBI's total memory
  // reservation fits into the guest's physical memory.
  zbi_header_t kernel_item_header;
  zbi_kernel_t kernel_payload_header;
  {
    if (auto ret = read(zbi_fd.get(), phys_mem.as<void>(kernel_zbi_off, sizeof(complete_zbi_t)),
                        sizeof(complete_zbi_t));
        ret != sizeof(complete_zbi_t)) {
      FX_LOGS(ERROR) << "Failed to read initial ZBI headers: " << strerror(errno);
      return ZX_ERR_IO;
    }
    if (auto ret = lseek(zbi_fd.get(), 0, SEEK_SET); ret) {
      FX_LOGS(ERROR) << "Failed to seek back to beginning of ZBI: " << strerror(errno);
      return ZX_ERR_IO;
    }

    // Dereference and copy for good measure, as we will soon be overwriting
    // the kernel ZBI range.
    kernel_item_header =
        *phys_mem.as<zbi_header_t>(kernel_zbi_off + offsetof(complete_zbi_t, hdr_kernel));
    kernel_payload_header =
        *phys_mem.as<zbi_kernel_t>(kernel_zbi_off + offsetof(complete_zbi_t, data_kernel));
  }
  const uint32_t reserved_size = offsetof(complete_zbi_t, data_kernel) + kernel_item_header.length +
                                 kernel_payload_header.reserve_memory_size;
  if (kernel_zbi_off + reserved_size > phys_mem.size()) {
    FX_LOGS(ERROR) << "Zircon kernel memory reservation exceeds guest physical memory";
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Check that the ZBI's total memory reservation does not overlap the
  // ramdisk.
  if (is_within(data_zbi_off, kernel_zbi_off, reserved_size)) {
    FX_LOGS(ERROR) << "Kernel reservation memory reservation overlaps RAM disk location";
    return ZX_ERR_OUT_OF_RANGE;
  }

  zbitl::View view(std::move(zbi_fd));
  if (auto result = zbitl::CheckComplete(view); result.is_error()) {
    FX_LOGS(ERROR) << "Incomplete ZBI: " << result.error_value();
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  auto first = view.begin();
  auto second = std::next(first);
  size_t kernel_zbi_size = second.item_offset();
  size_t data_zbi_size = view.size_bytes() - (second.item_offset() - first.item_offset());
  fbl::Span<std::byte> kernel_zbi{phys_mem.as<std::byte>(kernel_zbi_off), kernel_zbi_size};
  fbl::Span<std::byte> data_zbi{phys_mem.as<std::byte>(data_zbi_off), data_zbi_size};

  // Now that we have performed basic data integrity checks and know that the
  // kernel and data ZBI ranges do not overlap, copy.
  if (auto result = view.Copy(kernel_zbi, first, second); result.is_error()) {
    FX_LOGS(ERROR) << "Failed to create kernel ZBI: "
                   << zbitl::ViewCopyErrorString(result.error_value());
    view.ignore_error();
    return ZX_ERR_INTERNAL;
  }
  if (auto result = view.Copy(data_zbi, second, view.end()); result.is_error()) {
    FX_LOGS(ERROR) << zbitl::ViewCopyErrorString(result.error_value());
    view.ignore_error();
    return ZX_ERR_INTERNAL;
  }
  if (zx_status_t status = LogIfZbiError(view.take_error()); status != ZX_OK) {
    return status;
  }

  // On arm64, the kernel is relocatable so the entry point must be offset by
  // kKernelOffset. On x64, the entry point is absolute.
#if __aarch64__
  *guest_ip = kernel_payload_header.entry + kKernelOffset;
#elif __x86_64__
  *guest_ip = kernel_payload_header.entry;
#endif

  return ZX_OK;
}

static zx_status_t build_data_zbi(const fuchsia::virtualization::GuestConfig& cfg,
                                  const PhysMem& phys_mem, const DevMem& dev_mem,
                                  const std::vector<PlatformDevice*>& devices, uintptr_t zbi_off) {
  const size_t zbi_max = phys_mem.size() - zbi_off;
  fbl::Span<std::byte> zbi{phys_mem.as<std::byte>(zbi_off), zbi_max};
  zbitl::Image image(zbi);

  // Command line.
  const std::string cmdline = cfg.cmdline();
  zbitl::ByteView cmdline_bytes = zbitl::AsBytes(cmdline.data(), cmdline.size() + 1);
  zx_status_t status =
      LogIfZbiError(image.Append(zbi_header_t{.type = ZBI_TYPE_CMDLINE}, cmdline_bytes),
                    "Failed to append command-line item");
  if (status != ZX_OK) {
    return status;
  }

  // Any platform devices
  for (auto device : devices) {
    status = device->ConfigureZbi(zbi);
    if (status != ZX_OK) {
      return status;
    }
  }

#if __aarch64__
  // CPU config.
  uint8_t cpu_buffer[sizeof(zbi_cpu_config_t) + sizeof(zbi_cpu_cluster_t)] = {};
  auto cpu_config = reinterpret_cast<zbi_cpu_config_t*>(cpu_buffer);
  cpu_config->cluster_count = 1;
  cpu_config->clusters[0].cpu_count = cfg.cpus();
  status = LogIfZbiError(
      image.Append(zbi_header_t{.type = ZBI_TYPE_CPU_CONFIG}, zbitl::AsBytes(cpu_buffer)),
      "Failed to append CPU configuration");
  if (status != ZX_OK) {
    return status;
  }

  // Memory config.
  std::vector<zbi_mem_range_t> mem_config;
  auto yield = [&mem_config](zx_gpaddr_t addr, size_t size) {
    mem_config.emplace_back(zbi_mem_range_t{
        .paddr = addr,
        .length = size,
        .type = ZBI_MEM_RANGE_RAM,
    });
  };
  for (const fuchsia::virtualization::MemorySpec& spec : cfg.memory()) {
    // Do not use device memory when yielding normal memory.
    if (spec.policy != fuchsia::virtualization::MemoryPolicy::HOST_DEVICE) {
      dev_mem.YieldInverseRange(spec.base, spec.size, yield);
    }
  }

  // Zircon only supports a limited number of peripheral ranges so for any
  // dev_mem ranges that are not in the RAM range we will build a single
  // peripheral range to cover all of them.
  zbi_mem_range_t periph_range = {.paddr = 0, .length = 0, .type = ZBI_MEM_RANGE_PERIPHERAL};
  for (const auto& range : dev_mem) {
    if (range.addr < phys_mem.size()) {
      mem_config.emplace_back(zbi_mem_range_t{
          .paddr = range.addr,
          .length = range.size,
          .type = ZBI_MEM_RANGE_PERIPHERAL,
      });
    } else {
      if (periph_range.length == 0) {
        periph_range.paddr = range.addr;
      }
      periph_range.length = range.addr + range.size - periph_range.paddr;
    }
  }
  if (periph_range.length != 0) {
    mem_config.emplace_back(std::move(periph_range));
  }
  zbitl::ByteView mem_config_bytes =
      zbitl::AsBytes(mem_config.data(), sizeof(zbi_mem_range_t) * mem_config.size());
  status = LogIfZbiError(image.Append(zbi_header_t{.type = ZBI_TYPE_MEM_CONFIG}, mem_config_bytes),
                         "Failed to append memory configuration");
  if (status != ZX_OK) {
    return status;
  }

  // Platform ID.
  status = LogIfZbiError(
      image.Append(zbi_header_t{.type = ZBI_TYPE_PLATFORM_ID}, zbitl::AsBytes(kPlatformId)),
      "Failed to append platform ID");
  if (status != ZX_OK) {
    return status;
  }

  // PSCI driver.
  status = LogIfZbiError(image.Append(
                             zbi_header_t{
                                 .type = ZBI_TYPE_KERNEL_DRIVER,
                                 .extra = KDRV_ARM_PSCI,
                             },
                             zbitl::AsBytes(&kPsciDriver, sizeof(kPsciDriver))),
                         "Failed to append PSCI driver item");
  if (status != ZX_OK) {
    return status;
  }

  // Timer driver.
  status = LogIfZbiError(image.Append(
                             zbi_header_t{
                                 .type = ZBI_TYPE_KERNEL_DRIVER,
                                 .extra = KDRV_ARM_GENERIC_TIMER,
                             },
                             zbitl::AsBytes(kTimerDriver)),
                         "Failed to append timer driver item");
  if (status != ZX_OK) {
    return status;
  }
#elif __x86_64__
  // ACPI root table pointer.
  if (zx_status_t status = LogIfZbiError(
          image.Append(zbi_header_t{.type = ZBI_TYPE_ACPI_RSDP}, zbitl::AsBytes(kAcpiOffset)),
          "Failed to append root ACPI table pointer");
      status != ZX_OK) {
    return status;
  }

  // E820 memory map.
  E820Map e820_map(phys_mem.size(), dev_mem);
  for (const auto& range : dev_mem) {
    e820_map.AddReservedRegion(range.addr, range.size);
  }
  const uint32_t e820_size = e820_map.size() * sizeof(e820entry_t);
  auto result = image.Append(zbi_header_t{.type = ZBI_TYPE_E820_TABLE, .length = e820_size});
  LogIfZbiError(result);
  auto it = std::move(result).value();
  e820_map.copy(reinterpret_cast<e820entry_t*>((*it).payload.data()));
#endif

  return ZX_OK;
}

zx_status_t setup_zircon(fuchsia::virtualization::GuestConfig* cfg, const PhysMem& phys_mem,
                         const DevMem& dev_mem, const std::vector<PlatformDevice*>& devices,
                         uintptr_t* guest_ip, uintptr_t* boot_ptr) {
  fbl::unique_fd kernel_fd;
  zx_status_t status = fdio_fd_create(cfg->mutable_kernel()->TakeChannel().release(),
                                      kernel_fd.reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to open kernel image";
    return status;
  }
  status =
      read_unified_zbi(std::move(kernel_fd), kKernelOffset, kRamdiskOffset, phys_mem, guest_ip);
  if (status != ZX_OK) {
    return status;
  }

  status = build_data_zbi(*cfg, phys_mem, dev_mem, devices, kRamdiskOffset);
  if (status != ZX_OK) {
    return status;
  }

  *boot_ptr = kRamdiskOffset;
  return ZX_OK;
}
