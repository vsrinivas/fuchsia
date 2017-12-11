// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <hypervisor/guest.h>
#include <zircon/assert.h>
#include <zircon/boot/bootdata.h>

#include "garnet/bin/guest/efi.h"
#include "garnet/bin/guest/kernel.h"
#include "garnet/bin/guest/zircon.h"

static const uint64_t kBuildSigStartMagic = 0x5452545347495342;  // BSIGSTRT

static bool is_bootdata(const bootdata_t* header) {
  return header->type == BOOTDATA_CONTAINER &&
         header->length > sizeof(bootdata_t) &&
         header->extra == BOOTDATA_MAGIC && header->flags & BOOTDATA_FLAG_V2 &&
         header->magic == BOOTITEM_MAGIC;
}

static void set_bootdata(bootdata_t* header, uint32_t type, uint32_t len) {
  // Guest memory is initially zeroed, so we skip fields that must be zero.
  header->type = type;
  header->length = len;
  header->flags = BOOTDATA_FLAG_V2;
  header->magic = BOOTITEM_MAGIC;
  header->crc32 = BOOTITEM_NO_CRC32;
}

static zx_status_t load_zircon(const int fd,
                               const uintptr_t addr,
                               const uintptr_t first_page,
                               const uintptr_t kernel_off,
                               const size_t kernel_len) {
  // Move the first page to the kernel offset.
  void* kernel_loc = reinterpret_cast<void*>(addr + kernel_off);
  memmove(kernel_loc, reinterpret_cast<void*>(first_page), PAGE_SIZE);

  // Read in the rest of the kernel.
  const uintptr_t data_off = kernel_off + PAGE_SIZE;
  const size_t data_len = kernel_len - PAGE_SIZE;
  uint64_t* read_loc = reinterpret_cast<uint64_t*>(addr + data_off);
  const ssize_t ret = read(fd, read_loc, data_len);
  if (ret < 0 || static_cast<size_t>(ret) != data_len) {
    // We now need to distinguish between:
    // 1. Did we load a corrupted Zircon kernel image.
    // 2. Did we load an EFI kernel image for a different kernel.
    if (*read_loc != kBuildSigStartMagic)
      return ZX_ERR_NOT_SUPPORTED;
    fprintf(stderr, "Failed to read Zircon kernel image\n");
    return ZX_ERR_IO;
  }

  // Find build signature.
  void* signature = memmem(kernel_loc, kernel_len, &kBuildSigStartMagic,
                           sizeof(kBuildSigStartMagic));
  if (signature == nullptr)
    return ZX_ERR_NOT_SUPPORTED;

  return ZX_OK;
}

static zx_status_t load_cmdline(const char* cmdline,
                                const uintptr_t addr,
                                const size_t size,
                                const uintptr_t bootdata_off) {
  bootdata_t* container_hdr =
      reinterpret_cast<bootdata_t*>(addr + bootdata_off);
  const uintptr_t data_off =
      bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(container_hdr->length);

  const size_t cmdline_len = strlen(cmdline) + 1;
  if (cmdline_len > UINT32_MAX || data_off + cmdline_len > size) {
    fprintf(stderr, "Command line is too long\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  bootdata_t* cmdline_hdr = reinterpret_cast<bootdata_t*>(addr + data_off);
  set_bootdata(cmdline_hdr, BOOTDATA_CMDLINE,
               static_cast<uint32_t>(cmdline_len));
  memcpy(cmdline_hdr + 1, cmdline, cmdline_len);

  container_hdr->length += static_cast<uint32_t>(sizeof(bootdata_t)) +
                           BOOTDATA_ALIGN(cmdline_hdr->length);
  return ZX_OK;
}

static zx_status_t load_bootfs(const int fd,
                               const uintptr_t addr,
                               const size_t size,
                               const uintptr_t bootdata_off) {
  bootdata_t ramdisk_hdr;
  ssize_t ret = read(fd, &ramdisk_hdr, sizeof(bootdata_t));
  if (ret != sizeof(bootdata_t)) {
    fprintf(stderr, "Failed to read BOOTFS image header\n");
    return ZX_ERR_IO;
  }
  if (!is_bootdata(&ramdisk_hdr)) {
    fprintf(stderr, "Invalid BOOTFS image header\n");
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (ramdisk_hdr.length > size - bootdata_off) {
    fprintf(stderr, "BOOTFS image is too large\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  bootdata_t* container_hdr =
      reinterpret_cast<bootdata_t*>(addr + bootdata_off);
  uintptr_t data_off =
      bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(container_hdr->length);

  ret = read(fd, reinterpret_cast<void*>(addr + data_off), ramdisk_hdr.length);
  if (ret < 0 || (size_t)ret != ramdisk_hdr.length) {
    fprintf(stderr, "Failed to read BOOTFS image data\n");
    return ZX_ERR_IO;
  }

  container_hdr->length += BOOTDATA_ALIGN(ramdisk_hdr.length) +
                           static_cast<uint32_t>(sizeof(bootdata_t));
  return ZX_OK;
}

static zx_status_t create_bootdata(const uintptr_t addr,
                                   const size_t size,
                                   const uintptr_t acpi_off,
                                   uintptr_t bootdata_off) {
  if (BOOTDATA_ALIGN(bootdata_off) != bootdata_off)
    return ZX_ERR_INVALID_ARGS;

#if __aarch64__
  const size_t bootdata_len = 0;
#elif __x86_64__
  const size_t e820_size = guest_e820_size(size);
  const size_t bootdata_len = sizeof(bootdata_t) +
                              BOOTDATA_ALIGN(sizeof(uint64_t)) +
                              sizeof(bootdata_t) + BOOTDATA_ALIGN(e820_size);
#endif
  if (bootdata_off + bootdata_len + sizeof(bootdata_t) > size)
    return ZX_ERR_BUFFER_TOO_SMALL;

  // Bootdata container.
  bootdata_t* container_hdr =
      reinterpret_cast<bootdata_t*>(addr + bootdata_off);
  set_bootdata(container_hdr, BOOTDATA_CONTAINER,
               static_cast<uint32_t>(bootdata_len));
  container_hdr->extra = BOOTDATA_MAGIC;

#if __aarch64__
  return ZX_OK;
#elif __x86_64__
  // ACPI root table pointer.
  bootdata_off += sizeof(bootdata_t);
  bootdata_t* acpi_rsdp_hdr =
      reinterpret_cast<bootdata_t*>(addr + bootdata_off);
  set_bootdata(acpi_rsdp_hdr, BOOTDATA_ACPI_RSDP, sizeof(uint64_t));
  bootdata_off += sizeof(bootdata_t);
  *reinterpret_cast<uint64_t*>(addr + bootdata_off) = acpi_off;

  // E820 memory map.
  bootdata_off += BOOTDATA_ALIGN(sizeof(uint64_t));
  bootdata_t* e820_table_hdr =
      reinterpret_cast<bootdata_t*>(addr + bootdata_off);
  set_bootdata(e820_table_hdr, BOOTDATA_E820_TABLE,
               static_cast<uint32_t>(e820_size));
  bootdata_off += sizeof(bootdata_t);
  return guest_create_e820(addr, size, bootdata_off);
#endif
}

static zx_status_t read_bootdata(const uintptr_t first_page,
                                 uintptr_t* guest_ip,
                                 uintptr_t* kernel_off,
                                 size_t* kernel_len) {
  zircon_kernel_t* kernel_header =
      reinterpret_cast<zircon_kernel_t*>(first_page);

  if (!is_bootdata(&kernel_header->hdr_file)) {
    return read_efi(first_page, guest_ip, kernel_off, kernel_len);
  } else if (kernel_header->hdr_kernel.type != BOOTDATA_KERNEL) {
    fprintf(stderr, "Invalid Zircon kernel header\n");
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  *guest_ip = kernel_header->data_kernel.entry64;
  *kernel_off = kKernelOffset;
  *kernel_len =
      sizeof(bootdata_t) + BOOTDATA_ALIGN(kernel_header->hdr_file.length);
  return ZX_OK;
}

zx_status_t setup_zircon(const uintptr_t addr,
                         const size_t size,
                         const uintptr_t first_page,
                         const uintptr_t acpi_off,
                         const int fd,
                         const char* bootdata_path,
                         const char* cmdline,
                         uintptr_t* guest_ip,
                         uintptr_t* ramdisk_off) {
  uintptr_t kernel_off = 0;
  size_t kernel_len = 0;
  zx_status_t status =
      read_bootdata(first_page, guest_ip, &kernel_off, &kernel_len);
  if (status != ZX_OK)
    return status;
  if (!valid_location(size, *guest_ip, kernel_off, kernel_len))
    return ZX_ERR_IO_DATA_INTEGRITY;

  status = create_bootdata(addr, size, acpi_off, kRamdiskOffset);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to create BOOTDATA\n");
    return status;
  }

  status = load_zircon(fd, addr, first_page, kernel_off, kernel_len);
  if (status != ZX_OK)
    return status;

  // Load the kernel command line.
  status = load_cmdline(cmdline, addr, size, kRamdiskOffset);
  if (status != ZX_OK)
    return status;

  // If we have been provided a BOOTFS image, load it.
  if (bootdata_path) {
    fbl::unique_fd boot_fd(open(bootdata_path, O_RDONLY));
    if (!boot_fd) {
      fprintf(stderr, "Failed to open BOOTFS image \"%s\"\n", bootdata_path);
      return ZX_ERR_IO;
    }

    status = load_bootfs(boot_fd.get(), addr, size, kRamdiskOffset);
    if (status != ZX_OK)
      return status;
  }

  *ramdisk_off = kRamdiskOffset;
  return ZX_OK;
}
