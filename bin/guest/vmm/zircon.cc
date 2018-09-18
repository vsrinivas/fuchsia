// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/zircon.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <libzbi/zbi.h>
#include <zircon/assert.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/image.h>

#include "garnet/bin/guest/vmm/guest_config.h"
#include "garnet/bin/guest/vmm/kernel.h"
#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/dev_mem.h"
#include "garnet/lib/machina/guest.h"

#if __aarch64__
static constexpr uintptr_t kKernelOffset = 0;

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

#include "garnet/lib/machina/arch/x86/acpi.h"
#include "garnet/lib/machina/arch/x86/e820.h"
#endif

static bool is_zbi(const zbi_header_t* header) {
  return header->type == ZBI_TYPE_CONTAINER &&
         header->length > sizeof(zbi_header_t) &&
         header->extra == ZBI_CONTAINER_MAGIC &&
         header->flags & ZBI_FLAG_VERSION && header->magic == ZBI_ITEM_MAGIC;
}

static zx_status_t load_bootfs(const int fd, const machina::PhysMem& phys_mem,
                               const uintptr_t zbi_off) {
  zbi_header_t ramdisk_hdr;
  ssize_t ret = read(fd, &ramdisk_hdr, sizeof(zbi_header_t));
  if (ret != sizeof(zbi_header_t)) {
    FXL_LOG(ERROR) << "Failed to read BOOTFS image header";
    return ZX_ERR_IO;
  }
  if (!is_zbi(&ramdisk_hdr)) {
    FXL_LOG(ERROR) << "Invalid BOOTFS image header";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (ramdisk_hdr.length > phys_mem.size() - zbi_off) {
    FXL_LOG(ERROR) << "BOOTFS image is too large";
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto container_hdr = phys_mem.as<zbi_header_t>(zbi_off);
  const uintptr_t data_off =
      zbi_off + sizeof(zbi_header_t) + container_hdr->length;
  ret = read(fd, phys_mem.as<void>(data_off, ramdisk_hdr.length),
             ramdisk_hdr.length);
  if (ret < 0 || (size_t)ret != ramdisk_hdr.length) {
    FXL_LOG(ERROR) << "Failed to read BOOTFS image data";
    return ZX_ERR_IO;
  }

  container_hdr->length += ZBI_ALIGN(ramdisk_hdr.length);
  return ZX_OK;
}

static zx_status_t create_zbi(
    const GuestConfig& cfg, const machina::PhysMem& phys_mem,
    const machina::DevMem& dev_mem,
    const std::vector<machina::PlatformDevice*>& devices,
    const uintptr_t kernel_off, uintptr_t zbi_off) {
  if (ZBI_ALIGN(zbi_off) != zbi_off) {
    FXL_LOG(ERROR) << "ZBI offset has invalid alignment";
    return ZX_ERR_INVALID_ARGS;
  }

  // Create ZBI container.
  const size_t zbi_max = phys_mem.size() - zbi_off;
  auto container_hdr = phys_mem.as<zbi_header_t>(zbi_off);
  *container_hdr = ZBI_CONTAINER_HEADER(0);

  // TODO(PD-166): We should split reading the kernel ZBI item from reading the
  // additional ZBI items in the kernel ZBI container. This is so that we can
  // avoid the memcpy below.
  auto kernel_hdr = phys_mem.as<zircon_kernel_t>(kernel_off);
  const uint32_t file_len = sizeof(zbi_header_t) + kernel_hdr->hdr_file.length;
  const uint32_t kernel_len =
      offsetof(zircon_kernel_t, data_kernel) + kernel_hdr->hdr_kernel.length;

  // Copy additional items from the kernel ZBI container to our ZBI container.
  if (file_len > kernel_len) {
    const uint32_t items_len = file_len - kernel_len;
    const uintptr_t data_off =
        zbi_off + sizeof(zbi_header_t) + container_hdr->length;
    memcpy(phys_mem.as<void>(data_off, items_len),
           phys_mem.as<void>(kernel_off + kernel_len, items_len), items_len);
    container_hdr->length += ZBI_ALIGN(items_len);
  }

  // Update the kernel ZBI container header.
  kernel_hdr->hdr_file =
      ZBI_CONTAINER_HEADER(static_cast<uint32_t>(kernel_len));

  // Command line.
  zbi_result_t res;
  res = zbi_append_section(container_hdr, zbi_max, cfg.cmdline().size() + 1,
                           ZBI_TYPE_CMDLINE, 0, 0, cfg.cmdline().c_str());
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
  cpu_config->clusters[0].cpu_count = cfg.num_cpus();
  res = zbi_append_section(container_hdr, zbi_max, sizeof(cpu_buffer),
                           ZBI_TYPE_CPU_CONFIG, 0, 0, cpu_buffer);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  // Memory config.
  std::vector<zbi_mem_range_t> mem_config;

  dev_mem.YieldInverseRange(0, cfg.memory(), [&mem_config](auto range){
    mem_config.emplace_back(zbi_mem_range_t{
      .paddr = range.addr,
      .length = range.size,
      .type = ZBI_MEM_RANGE_RAM,
    });
  });

  for (const auto& range: dev_mem) {
    mem_config.emplace_back(zbi_mem_range_t{
        .paddr = range.addr,
        .length = range.size,
        .type = ZBI_MEM_RANGE_PERIPHERAL,
    });
  }
  res = zbi_append_section(container_hdr, zbi_max,
                           sizeof(zbi_mem_range_t) * mem_config.size(),
                           ZBI_TYPE_MEM_CONFIG, 0, 0, &mem_config[0]);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  // Platform ID.
  res = zbi_append_section(container_hdr, zbi_max, sizeof(kPlatformId),
                           ZBI_TYPE_PLATFORM_ID, 0, 0, &kPlatformId);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  // PSCI driver.
  res = zbi_append_section(container_hdr, zbi_max, sizeof(kPsciDriver),
                           ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_PSCI, 0,
                           &kPsciDriver);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  // Timer driver.
  res = zbi_append_section(container_hdr, zbi_max, sizeof(kTimerDriver),
                           ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER, 0,
                           &kTimerDriver);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
#elif __x86_64__
  // ACPI root table pointer.
  res = zbi_append_section(container_hdr, zbi_max, sizeof(uint64_t),
                           ZBI_TYPE_ACPI_RSDP, 0, 0, &machina::kAcpiOffset);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  // E820 memory map.
  machina::E820Map e820_map(phys_mem.size(), dev_mem);
  for (const auto& range : dev_mem) {
    e820_map.AddReservedRegion(range.addr, range.size);
  }
  const size_t e820_size = e820_map.size() * sizeof(e820entry_t);
  void* e820_addr = nullptr;
  res = zbi_create_section(container_hdr, zbi_max, e820_size,
                           ZBI_TYPE_E820_TABLE, 0, 0, &e820_addr);
  if (res != ZBI_RESULT_OK) {
    return ZX_ERR_INTERNAL;
  }
  e820_map.copy(static_cast<e820entry_t*>(e820_addr));
#endif
  return ZX_OK;
}

static zx_status_t check_kernel(const machina::PhysMem& phys_mem,
                                const uintptr_t kernel_off,
                                uintptr_t* guest_ip) {
  auto kernel_hdr = phys_mem.as<zircon_kernel_t>(kernel_off);
  zbi_result_t res = zbi_check(kernel_hdr, nullptr);
  if (res != ZBI_RESULT_OK ||
      !ZBI_IS_KERNEL_BOOTITEM(kernel_hdr->hdr_kernel.type)) {
    FXL_LOG(ERROR) << "Invalid Zircon container";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (kernel_off + offsetof(zircon_kernel_t, data_kernel) +
          kernel_hdr->hdr_kernel.length +
          kernel_hdr->data_kernel.reserve_memory_size >
      phys_mem.size()) {
    FXL_LOG(ERROR)
        << "Zircon kernel memory reservation exceeds guest physical memory";
    return ZX_ERR_OUT_OF_RANGE;
  }
  // TODO(PD-166): Reject kernel if file doesn't match size in header.
  *guest_ip = kernel_hdr->data_kernel.entry;
  return ZX_OK;
}

zx_status_t setup_zircon(const GuestConfig& cfg,
                         const machina::PhysMem& phys_mem,
                         const machina::DevMem& dev_mem,
                         const std::vector<machina::PlatformDevice*>& devices,
                         uintptr_t* guest_ip, uintptr_t* boot_ptr) {
  // Read the kernel image.
  zx_status_t status = load_kernel(cfg.kernel_path(), phys_mem, kKernelOffset);
  if (status != ZX_OK) {
    return status;
  }
  status = check_kernel(phys_mem, kKernelOffset, guest_ip);
  if (status != ZX_OK) {
    return status;
  }

  // Create the ZBI container.
  status = create_zbi(cfg, phys_mem, dev_mem, devices, kKernelOffset,
                      kRamdiskOffset);
  if (status != ZX_OK) {
    return status;
  }

  // If we have been provided a BOOTFS image, load it.
  if (!cfg.ramdisk_path().empty()) {
    fbl::unique_fd boot_fd(open(cfg.ramdisk_path().c_str(), O_RDONLY));
    if (!boot_fd) {
      FXL_LOG(ERROR) << "Failed to open BOOTFS image " << cfg.ramdisk_path();
      return ZX_ERR_IO;
    }

    status = load_bootfs(boot_fd.get(), phys_mem, kRamdiskOffset);
    if (status != ZX_OK) {
      return status;
    }
  }

  *boot_ptr = kRamdiskOffset;
  return ZX_OK;
}
