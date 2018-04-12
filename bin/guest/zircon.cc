// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/zircon.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zircon/assert.h>
#include <zircon/boot/bootdata.h>
#include <zircon/boot/driver-config.h>

#include "garnet/bin/guest/kernel.h"
#include "garnet/lib/machina/guest.h"

#if __aarch64__
static constexpr uintptr_t kKernelOffset = 0;

static bootdata_mem_range_t mem_config[] = {
    {
        .type = BOOTDATA_MEM_RANGE_RAM,
        .paddr = 0,
        .length = 0x40000000,  // set to phys_mem.size()
    },
    {
        .type = BOOTDATA_MEM_RANGE_PERIPHERAL,
        .paddr = 0xe8100000,
        .length = 0x17f00000,
    },
    {
        // Reserved for RTC
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x09010000,
        .length = 0x1000,  // 4KB
    },
    {
        // Reserved for MMIO
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x06fe0000,
        .length = 0x1000000,  // 16MB
    },
    {
        // Reserved for ECAM
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x2e000000,
        .length = 0x1000000,  // 16MB
    },
};

static constexpr bootdata_platform_id_t kPlatformId = {
    .vid = 3,  // PDEV_VID_GOOGLE
    .pid = 2,  // PDEV_PID_MACHINA
    .board_name = "machina",
};

static constexpr dcfg_simple_t kUartDriver = {
    .mmio_phys = 0xfff32000,
    .irq = 111,
};

static constexpr dcfg_arm_gicv2_driver_t kGicV2Driver = {
    .mmio_phys = 0xe82b0000,
    .gicd_offset = 0x1000,
    .gicc_offset = 0x2000,
    .gich_offset = 0x4000,
    .gicv_offset = 0x6000,
    .ipi_base = 13,
    .optional = true,
    .use_msi = true,
};

static constexpr dcfg_arm_gicv3_driver_t kGicV3Driver = {
    .mmio_phys = 0xe82b0000,
    .gicd_offset = 0x00000,
    .gicr_offset = 0xa0000,
    .gicr_stride = 0x20000,
    .ipi_base = 13,
    .optional = true,
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

static bool is_bootdata(const bootdata_t* header) {
  return header->type == BOOTDATA_CONTAINER &&
         header->length > sizeof(bootdata_t) &&
         header->extra == BOOTDATA_MAGIC && header->flags & BOOTDATA_FLAG_V2 &&
         header->magic == BOOTITEM_MAGIC;
}

static void set_bootdata(bootdata_t* header,
                         uint32_t type,
                         uint32_t extra,
                         uint32_t len) {
  // Guest memory is initially zeroed, so we skip fields that must be zero.
  header->type = type;
  header->length = len;
  header->extra = extra;
  header->flags = BOOTDATA_FLAG_V2;
  header->magic = BOOTITEM_MAGIC;
  header->crc32 = BOOTITEM_NO_CRC32;
}

static zx_status_t load_cmdline(const std::string& cmdline,
                                const machina::PhysMem& phys_mem,
                                const uintptr_t bootdata_off) {
  auto container_hdr = phys_mem.as<bootdata_t>(bootdata_off);
  const uintptr_t data_off =
      bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(container_hdr->length);

  const size_t cmdline_len = cmdline.size() + 1;
  if (cmdline_len > UINT32_MAX || data_off + cmdline_len > phys_mem.size()) {
    FXL_LOG(ERROR) << "Command line is too long";
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto cmdline_hdr = phys_mem.as<bootdata_t>(data_off);
  set_bootdata(cmdline_hdr, BOOTDATA_CMDLINE, 0,
               static_cast<uint32_t>(cmdline_len));
  memcpy(cmdline_hdr + 1, cmdline.c_str(), cmdline_len);

  container_hdr->length += static_cast<uint32_t>(sizeof(bootdata_t)) +
                           BOOTDATA_ALIGN(cmdline_hdr->length);
  return ZX_OK;
}

static zx_status_t load_bootfs(const int fd,
                               const machina::PhysMem& phys_mem,
                               const uintptr_t bootdata_off) {
  bootdata_t ramdisk_hdr;
  ssize_t ret = read(fd, &ramdisk_hdr, sizeof(bootdata_t));
  if (ret != sizeof(bootdata_t)) {
    FXL_LOG(ERROR) << "Failed to read BOOTFS image header";
    return ZX_ERR_IO;
  }
  if (!is_bootdata(&ramdisk_hdr)) {
    FXL_LOG(ERROR) << "Invalid BOOTFS image header";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (ramdisk_hdr.length > phys_mem.size() - bootdata_off) {
    FXL_LOG(ERROR) << "BOOTFS image is too large";
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto container_hdr = phys_mem.as<bootdata_t>(bootdata_off);
  uintptr_t data_off =
      bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(container_hdr->length);

  ret = read(fd, phys_mem.as<void>(data_off, ramdisk_hdr.length),
             ramdisk_hdr.length);
  if (ret < 0 || (size_t)ret != ramdisk_hdr.length) {
    FXL_LOG(ERROR) << "Failed to read BOOTFS image data";
    return ZX_ERR_IO;
  }

  container_hdr->length += BOOTDATA_ALIGN(ramdisk_hdr.length) +
                           static_cast<uint32_t>(sizeof(bootdata_t));

  return ZX_OK;
}

static zx_status_t create_bootdata(const machina::PhysMem& phys_mem,
                                   uintptr_t bootdata_off,
                                   uint32_t num_cpus) {
  if (BOOTDATA_ALIGN(bootdata_off) != bootdata_off) {
    return ZX_ERR_INVALID_ARGS;
  }

#if __aarch64__
  // bootdata_cpu_config_t is variable length
  uint8_t cpu_config[sizeof(bootdata_cpu_config_t) +
                     sizeof(bootdata_cpu_cluster_t)] = {};
  auto header = reinterpret_cast<bootdata_cpu_config_t*>(cpu_config);
  header->cluster_count = 1;
  header->clusters[0].cpu_count = num_cpus;

  const size_t bootdata_len =
      BOOTDATA_ALIGN(sizeof(cpu_config)) + sizeof(bootdata_t) +
      BOOTDATA_ALIGN(sizeof(mem_config)) + sizeof(bootdata_t) +
      BOOTDATA_ALIGN(sizeof(kPlatformId)) + sizeof(bootdata_t) +
      BOOTDATA_ALIGN(sizeof(kUartDriver)) + sizeof(bootdata_t) +
      BOOTDATA_ALIGN(sizeof(kGicV2Driver)) + sizeof(bootdata_t) +
      BOOTDATA_ALIGN(sizeof(kGicV3Driver)) + sizeof(bootdata_t) +
      BOOTDATA_ALIGN(sizeof(kPsciDriver)) + sizeof(bootdata_t) +
      BOOTDATA_ALIGN(sizeof(kTimerDriver)) + sizeof(bootdata_t);
#elif __x86_64__
  const size_t e820_size = machina::e820_size(phys_mem.size());
  const size_t bootdata_len = sizeof(bootdata_t) +
                              BOOTDATA_ALIGN(sizeof(uint64_t)) +
                              sizeof(bootdata_t) + BOOTDATA_ALIGN(e820_size);
#endif
  if (bootdata_off + bootdata_len + sizeof(bootdata_t) > phys_mem.size()) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  // Bootdata container.
  auto container_hdr = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(container_hdr, BOOTDATA_CONTAINER, BOOTDATA_MAGIC,
               static_cast<uint32_t>(bootdata_len));
  bootdata_off += sizeof(bootdata_t);

#if __aarch64__
  // CPU config
  auto bootdata_header = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(bootdata_header, BOOTDATA_CPU_CONFIG, 0, sizeof(cpu_config));
  memcpy(bootdata_header + 1, &cpu_config, sizeof(cpu_config));
  bootdata_off += sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(cpu_config));

  // Memory config
  mem_config[0].length = phys_mem.size();
  bootdata_header = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(bootdata_header, BOOTDATA_MEM_CONFIG, 0, sizeof(mem_config));
  memcpy(bootdata_header + 1, &mem_config, sizeof(mem_config));
  bootdata_off += sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(mem_config));

  // platform ID
  bootdata_header = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(bootdata_header, BOOTDATA_PLATFORM_ID, 0, sizeof(kPlatformId));
  memcpy(bootdata_header + 1, &kPlatformId, sizeof(kPlatformId));
  bootdata_off += sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(kPlatformId));

  // uart driver
  bootdata_header = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(bootdata_header, BOOTDATA_KERNEL_DRIVER, KDRV_PL011_UART,
               sizeof(kUartDriver));
  memcpy(bootdata_header + 1, &kUartDriver, sizeof(kUartDriver));
  bootdata_off += sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(kUartDriver));

  // gicv2 driver
  bootdata_header = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(bootdata_header, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_GIC_V2,
               sizeof(kGicV2Driver));
  memcpy(bootdata_header + 1, &kGicV2Driver, sizeof(kGicV2Driver));
  bootdata_off += sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(kGicV2Driver));

  // gicv3 driver
  bootdata_header = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(bootdata_header, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_GIC_V3,
               sizeof(kGicV3Driver));
  memcpy(bootdata_header + 1, &kGicV3Driver, sizeof(kGicV3Driver));
  bootdata_off += sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(kGicV3Driver));

  // psci driver
  bootdata_header = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(bootdata_header, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_PSCI,
               sizeof(kPsciDriver));
  memcpy(bootdata_header + 1, &kPsciDriver, sizeof(kPsciDriver));
  bootdata_off += sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(kPsciDriver));

  // timer driver
  bootdata_header = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(bootdata_header, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER,
               sizeof(kTimerDriver));
  memcpy(bootdata_header + 1, &kTimerDriver, sizeof(kTimerDriver));
  bootdata_off += sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(kTimerDriver));

  return ZX_OK;
#elif __x86_64__
  // ACPI root table pointer.
  auto acpi_rsdp_hdr = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(acpi_rsdp_hdr, BOOTDATA_ACPI_RSDP, 0, sizeof(uint64_t));
  bootdata_off += sizeof(bootdata_t);
  *phys_mem.as<uint64_t>(bootdata_off) = machina::kAcpiOffset;

  // E820 memory map.
  bootdata_off += BOOTDATA_ALIGN(sizeof(uint64_t));
  auto e820_table_hdr = phys_mem.as<bootdata_t>(bootdata_off);
  set_bootdata(e820_table_hdr, BOOTDATA_E820_TABLE, 0,
               static_cast<uint32_t>(e820_size));
  bootdata_off += sizeof(bootdata_t);
  return machina::create_e820(phys_mem, bootdata_off);
#endif
}

static zx_status_t read_bootdata(const machina::PhysMem& phys_mem,
                                 uintptr_t* guest_ip) {
  auto kernel_hdr = phys_mem.as<zircon_kernel_t>(kKernelOffset);
  if (kernel_hdr->hdr_kernel.type != BOOTDATA_KERNEL) {
    FXL_LOG(ERROR) << "Invalid Zircon kernel header";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  *guest_ip = kernel_hdr->data_kernel.entry64;
  return ZX_OK;
}

zx_status_t setup_zircon(const GuestConfig cfg,
                         const machina::PhysMem& phys_mem,
                         uintptr_t* guest_ip,
                         uintptr_t* boot_ptr) {
  // Read the kernel image.
  zx_status_t status = load_kernel(cfg.kernel_path(), phys_mem, kKernelOffset);
  if (status != ZX_OK) {
    return status;
  }
  status = read_bootdata(phys_mem, guest_ip);
  if (status != ZX_OK) {
    return status;
  }

  // Create the BOOTDATA container.
  status = create_bootdata(phys_mem, kRamdiskOffset, cfg.num_cpus());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create BOOTDATA";
    return status;
  }

  // Load the kernel command line.
  status = load_cmdline(cfg.cmdline(), phys_mem, kRamdiskOffset);
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
