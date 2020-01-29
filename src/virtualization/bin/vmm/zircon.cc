// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/zircon.h"

#include <fcntl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/image.h>

#include <fbl/unique_fd.h>
#include <libzbi/zbi.h>

#include "src/virtualization/bin/vmm/dev_mem.h"
#include "src/virtualization/bin/vmm/guest.h"

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

zx_status_t read_unified_zbi(const std::string& zbi_path, const uintptr_t kernel_off,
                             const uintptr_t zbi_off, const PhysMem& phys_mem,
                             uintptr_t* guest_ip) {
  fbl::unique_fd fd(open(zbi_path.c_str(), O_RDONLY));
  if (!fd) {
    FXL_LOG(ERROR) << "Failed to open kernel image " << zbi_path;
    return ZX_ERR_IO;
  }
  struct stat stat;
  ssize_t ret = fstat(fd.get(), &stat);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to stat kernel image " << zbi_path;
    return ZX_ERR_IO;
  }

  if (ZBI_ALIGN(kernel_off) != kernel_off) {
    FXL_LOG(ERROR) << "Kernel offset has invalid alignment";
    return ZX_ERR_INVALID_ARGS;
  }

  // First read just the kernel header.
  ret = read(fd.get(), phys_mem.as<void>(kernel_off, sizeof(zircon_kernel_t)),
             sizeof(zircon_kernel_t));
  if (ret != sizeof(zircon_kernel_t)) {
    FXL_LOG(ERROR) << "Failed to read kernel header";
    return ZX_ERR_IO;
  }

  // Check that the kernel ZBI is the correct type.
  auto kernel_hdr = phys_mem.as<zircon_kernel_t>(kernel_off);
  if (!ZBI_IS_KERNEL_BOOTITEM(kernel_hdr->hdr_kernel.type)) {
    FXL_LOG(ERROR) << "Invalid Zircon container";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Check that the total size of the ZBI matches the file size.
  const uint32_t file_len = sizeof(zbi_header_t) + kernel_hdr->hdr_file.length;
  if (stat.st_size != file_len) {
    FXL_LOG(ERROR) << "ZBI length does not match file size";
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Check that the kernel's total memory reservation fits into guest physical
  // memory.
  const uint32_t reserved_size = offsetof(zircon_kernel_t, data_kernel) +
                                 kernel_hdr->hdr_kernel.length +
                                 kernel_hdr->data_kernel.reserve_memory_size;
  if (kernel_off + reserved_size > phys_mem.size()) {
    FXL_LOG(ERROR) << "Zircon kernel memory reservation exceeds guest physical memory";
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Check that the kernel's total memory reservation does not overlap the
  // ramdisk.
  if (is_within(zbi_off, kernel_off, reserved_size)) {
    FXL_LOG(ERROR) << "Kernel reservation memory reservation overlaps RAM disk location";
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Read the kernel payload.
  const uint32_t kernel_payload_len = kernel_hdr->hdr_kernel.length - sizeof(zbi_kernel_t);
  ret =
      read(fd.get(),
           phys_mem.as<void>(kernel_off + offsetof(zircon_kernel_t, contents), kernel_payload_len),
           kernel_payload_len);
  if (ret != kernel_payload_len) {
    FXL_LOG(ERROR) << "Failed to read kernel payload";
    return ZX_ERR_IO;
  }

  // Update the kernel ZBI container header and check that it is valid.
  kernel_hdr->hdr_file = ZBI_CONTAINER_HEADER(kernel_hdr->hdr_kernel.length +
                                              static_cast<uint32_t>(sizeof(zbi_header_t)));
  zbi_result_t res = zbi_check(kernel_hdr, nullptr);
  if (res != ZBI_RESULT_OK) {
    FXL_LOG(ERROR) << "Invalid kernel ZBI " << res;
    return ZX_ERR_INTERNAL;
  }

  // Create a separate data ZBI.

  if (ZBI_ALIGN(zbi_off) != zbi_off) {
    FXL_LOG(ERROR) << "ZBI offset has invalid alignment";
    return ZX_ERR_INVALID_ARGS;
  }
  auto container_hdr = phys_mem.as<zbi_header_t>(zbi_off);
  *container_hdr = ZBI_CONTAINER_HEADER(0);

  // Read additional items from the kernel ZBI container to the data ZBI.
  const uint32_t kernel_end =
      offsetof(zircon_kernel_t, data_kernel) + kernel_hdr->hdr_kernel.length;
  if (file_len > kernel_end) {
    const uint32_t items_len = file_len - kernel_end;
    const uintptr_t data_off = zbi_off + sizeof(zbi_header_t) + container_hdr->length;
    ret = read(fd.get(), phys_mem.as<void>(data_off, items_len), items_len);
    container_hdr->length += ZBI_ALIGN(items_len);
  }

  // On arm64, the kernel is relocatable so the entry point must be offset by
  // kKernelOffset. On x64, the entry point is absolute.
#if __aarch64__
  *guest_ip = kernel_hdr->data_kernel.entry + kKernelOffset;
#elif __x86_64__
  *guest_ip = kernel_hdr->data_kernel.entry;
#endif

  return ZX_OK;
}

static zx_status_t build_data_zbi(const fuchsia::virtualization::GuestConfig& cfg,
                                  const PhysMem& phys_mem, const DevMem& dev_mem,
                                  const std::vector<PlatformDevice*>& devices, uintptr_t zbi_off) {
  auto container_hdr = phys_mem.as<zbi_header_t>(zbi_off);
  const size_t zbi_max = phys_mem.size() - zbi_off;

  // Command line.
  zbi_result_t res;
  res = zbi_append_section(container_hdr, zbi_max, cfg.cmdline().size() + 1, ZBI_TYPE_CMDLINE, 0, 0,
                           cfg.cmdline().c_str());
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }

  // Any platform devices
  for (auto device : devices) {
    zx_status_t status = device->ConfigureZbi(container_hdr, zbi_max);
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
  res = zbi_append_section(container_hdr, zbi_max, sizeof(cpu_buffer), ZBI_TYPE_CPU_CONFIG, 0, 0,
                           cpu_buffer);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
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
  res = zbi_append_section(container_hdr, zbi_max, sizeof(zbi_mem_range_t) * mem_config.size(),
                           ZBI_TYPE_MEM_CONFIG, 0, 0, &mem_config[0]);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  // Platform ID.
  res = zbi_append_section(container_hdr, zbi_max, sizeof(kPlatformId), ZBI_TYPE_PLATFORM_ID, 0, 0,
                           &kPlatformId);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  // PSCI driver.
  res = zbi_append_section(container_hdr, zbi_max, sizeof(kPsciDriver), ZBI_TYPE_KERNEL_DRIVER,
                           KDRV_ARM_PSCI, 0, &kPsciDriver);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  // Timer driver.
  res = zbi_append_section(container_hdr, zbi_max, sizeof(kTimerDriver), ZBI_TYPE_KERNEL_DRIVER,
                           KDRV_ARM_GENERIC_TIMER, 0, &kTimerDriver);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
#elif __x86_64__
  // ACPI root table pointer.
  res = zbi_append_section(container_hdr, zbi_max, sizeof(uint64_t), ZBI_TYPE_ACPI_RSDP, 0, 0,
                           &kAcpiOffset);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  // E820 memory map.
  E820Map e820_map(phys_mem.size(), dev_mem);
  for (const auto& range : dev_mem) {
    e820_map.AddReservedRegion(range.addr, range.size);
  }
  const size_t e820_size = e820_map.size() * sizeof(e820entry_t);
  void* e820_addr = nullptr;
  res =
      zbi_create_section(container_hdr, zbi_max, e820_size, ZBI_TYPE_E820_TABLE, 0, 0, &e820_addr);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  e820_map.copy(static_cast<e820entry_t*>(e820_addr));
#endif

  res = zbi_check(container_hdr, nullptr);
  if (res != ZBI_RESULT_OK) {
    FXL_LOG(ERROR) << "Invalid Zircon container: " << res;
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t setup_zircon(const fuchsia::virtualization::GuestConfig& cfg, const PhysMem& phys_mem,
                         const DevMem& dev_mem, const std::vector<PlatformDevice*>& devices,
                         uintptr_t* guest_ip, uintptr_t* boot_ptr) {
  zx_status_t status =
      read_unified_zbi(cfg.kernel_path(), kKernelOffset, kRamdiskOffset, phys_mem, guest_ip);

  if (status != ZX_OK) {
    return status;
  }

  status = build_data_zbi(cfg, phys_mem, dev_mem, devices, kRamdiskOffset);
  if (status != ZX_OK) {
    return status;
  }

  *boot_ptr = kRamdiskOffset;
  return ZX_OK;
}
