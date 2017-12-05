// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <hypervisor/bits.h>
#include <hypervisor/guest.h>

#include "garnet/bin/guest/kernel.h"
#include "garnet/bin/guest/linux.h"

static const uint8_t kLoaderTypeUnspecified = 0xff;  // Unknown bootloader
static const uint16_t kMinBootProtocol = 0x200;      // bzImage boot protocol
static const uint16_t kBootFlagMagic = 0xaa55;
static const uint32_t kHeaderMagic = 0x53726448;
static const uintptr_t kE820MapOffset = 0x02d0;
static const uintptr_t kEntryOffset = 0x200;
static const size_t kMaxE820Entries = 128;
static const size_t kSectorSize = 512;

// clang-format off

// For the Linux x86 boot protocol, see:
// https://www.kernel.org/doc/Documentation/x86/boot.txt
// https://www.kernel.org/doc/Documentation/x86/zero-page.txt

enum Bp8 : uintptr_t {
  VIDEO_MODE                = 0x0006,   // Original video mode
  VIDEO_COLS                = 0x0007,   // Original video cols
  VIDEO_LINES               = 0x000e,   // Original video lines
  E820_COUNT                = 0x01e8,   // Number of entries in e820 map
  SETUP_SECTS               = 0x01f1,   // Size of real mode kernel in sectors
  LOADER_TYPE               = 0x0210,   // Type of bootloader
  LOADFLAGS                 = 0x0211,   // Boot protocol flags
  RELOCATABLE               = 0x0234,   // Is the kernel relocatable?
};

enum Bp16 : uintptr_t {
  BOOTFLAG                  = 0x01fe,   // Bootflag, should match BOOT_FLAG_MAGIC
  VERSION                   = 0x0206,   // Boot protocol version
  XLOADFLAGS                = 0x0236,   // 64-bit and EFI load flags
};

enum Bp32 : uintptr_t {
  SYSSIZE                   = 0x01f4,   // Size of protected-mode code + payload in 16-bytes
  HEADER                    = 0x0202,   // Header, should match HEADER_MAGIC
  RAMDISK_IMAGE             = 0x0218,   // Ramdisk image address
  RAMDISK_SIZE              = 0x021c,   // Ramdisk image size
  COMMAND_LINE              = 0x0228,   // Pointer to command line args string
  KERNEL_ALIGN              = 0x0230,   // Kernel alignment
};

enum Bp64 : uintptr_t {
  PREF_ADDRESS              = 0x0258,   // Preferred address for kernel to be loaded at
};

enum Lf : uint8_t {
  LOAD_HIGH                 = 1u << 0,  // Protected mode code loads at 0x100000
};

enum Xlf : uint16_t {
  KERNEL_64                 = 1u << 0,  // Has legacy 64-bit entry point at 0x200
  CAN_BE_LOADED_ABOVE_4G    = 1u << 1,  // Kernel/boot_params/cmdline/ramdisk can be above 4G
};

// clang-format on

static uint8_t& bp(uintptr_t addr, Bp8 off) {
  return *reinterpret_cast<uint8_t*>(addr + off);
}

static uint16_t& bp(uintptr_t addr, Bp16 off) {
  return *reinterpret_cast<uint16_t*>(addr + off);
}

static uint32_t& bp(uintptr_t addr, Bp32 off) {
  return *reinterpret_cast<uint32_t*>(addr + off);
}

static uint64_t& bp(uintptr_t addr, Bp64 off) {
  return *reinterpret_cast<uint64_t*>(addr + off);
}

static bool is_boot_params(const uintptr_t first_page) {
  return bp(first_page, BOOTFLAG) == kBootFlagMagic &&
         bp(first_page, HEADER) == kHeaderMagic;
}

static zx_status_t load_linux(const int fd,
                              const uintptr_t addr,
                              const uintptr_t first_page,
                              const uintptr_t kernel_off,
                              const size_t kernel_len) {
  int setup_sects = bp(first_page, SETUP_SECTS);
  if (setup_sects == 0) {
    // 0 here actually means 4, see boot.txt.
    setup_sects = 4;
  }

  int protected_mode_off = (setup_sects + 1) * kSectorSize;
  off_t ret = lseek(fd, protected_mode_off, SEEK_SET);
  if (ret < 0) {
    fprintf(stderr, "Failed seek to protected mode kernel\n");
    return ZX_ERR_IO;
  }

  ret = read(fd, reinterpret_cast<void*>(addr + kernel_off), kernel_len);
  if (static_cast<size_t>(ret) != kernel_len) {
    fprintf(stderr, "Failed to read Linux kernel data\n");
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

static zx_status_t load_cmdline(const char* cmdline,
                                const uintptr_t addr,
                                const size_t size,
                                const uintptr_t first_page,
                                const uintptr_t cmdline_off) {
  // Copy the command line string below the zero page.
  size_t cmdline_len = strlen(cmdline) + 1;
  if (cmdline_off > UINT32_MAX || cmdline_len > size - cmdline_off) {
    fprintf(stderr, "Command line is too long\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  memcpy(reinterpret_cast<void*>(addr + cmdline_off), cmdline, cmdline_len);

  bp(first_page, COMMAND_LINE) = static_cast<uint32_t>(cmdline_off);
  return ZX_OK;
}

static zx_status_t load_initrd(const int fd,
                               const uintptr_t addr,
                               const size_t size,
                               const uintptr_t first_page,
                               const uintptr_t bootdata_off) {
  struct stat stat;
  off_t ret = fstat(fd, &stat);
  if (ret < 0) {
    fprintf(stderr, "Failed to stat initial RAM disk\n");
    return ZX_ERR_IO;
  }

  size_t initrd_size = stat.st_size;
  if (initrd_size > UINT32_MAX || initrd_size > size - bootdata_off) {
    fprintf(stderr, "Initial RAM disk is too large\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  ret = read(fd, reinterpret_cast<void*>(addr + bootdata_off), initrd_size);
  if (static_cast<size_t>(ret) != initrd_size) {
    fprintf(stderr, "Failed to read initial RAM disk\n");
    return ZX_ERR_IO;
  }

  bp(first_page, RAMDISK_IMAGE) = kRamdiskOffset;
  bp(first_page, RAMDISK_SIZE) = static_cast<uint32_t>(initrd_size);
  return ZX_OK;
}

static zx_status_t create_boot_params(const uintptr_t addr,
                                      const size_t size,
                                      const uintptr_t first_page) {
  // Set type of bootloader.
  bp(first_page, LOADER_TYPE) = kLoaderTypeUnspecified;

  // Zero video, columns and lines to skip early video init.
  bp(first_page, VIDEO_MODE) = 0;
  bp(first_page, VIDEO_COLS) = 0;
  bp(first_page, VIDEO_LINES) = 0;

  // Setup e820 memory map.
  size_t e820_entries = guest_e820_size(size) / sizeof(e820entry_t);
  if (e820_entries > kMaxE820Entries) {
    fprintf(stderr, "Not enough space for e820 memory map\n");
    return ZX_ERR_BAD_STATE;
  }
  bp(first_page, E820_COUNT) = static_cast<uint8_t>(e820_entries);

  return guest_create_e820(addr, size, first_page - addr + kE820MapOffset);
}

static zx_status_t is_linux(const uintptr_t first_page,
                            uintptr_t* guest_ip,
                            uintptr_t* kernel_off,
                            size_t* kernel_len) {
  uint16_t xloadflags = bp(first_page, XLOADFLAGS);
  if (~xloadflags & (KERNEL_64 | CAN_BE_LOADED_ABOVE_4G)) {
    fprintf(stderr, "Unsupported kernel type\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint16_t protocol = bp(first_page, VERSION);
  uint8_t loadflags = bp(first_page, LOADFLAGS);
  if (protocol < kMinBootProtocol || !(loadflags & LOAD_HIGH)) {
    fprintf(stderr, "Kernel is not a bzImage, use a newer kernel\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Default to the preferred address, then change if we're relocatable.
  uintptr_t runtime_start = bp(first_page, PREF_ADDRESS);
  if (bp(first_page, RELOCATABLE) > 0) {
    uint64_t kernel_align = bp(first_page, KERNEL_ALIGN);
    runtime_start = align(kKernelOffset, kernel_align);
  }

  *guest_ip = runtime_start + kEntryOffset;
  *kernel_off = runtime_start;
  *kernel_len = bp(first_page, SYSSIZE) << 4;
  return ZX_OK;
}

zx_status_t setup_linux(const uintptr_t addr,
                        const size_t size,
                        const uintptr_t first_page,
                        const int fd,
                        const char* initrd_path,
                        const char* cmdline,
                        uintptr_t* guest_ip,
                        uintptr_t* ramdisk_off) {
  if (!is_boot_params(first_page))
    return ZX_ERR_NOT_SUPPORTED;

  uintptr_t kernel_off = 0;
  size_t kernel_len = 0;
  zx_status_t status = is_linux(first_page, guest_ip, &kernel_off, &kernel_len);
  if (status != ZX_OK)
    return ZX_ERR_NOT_SUPPORTED;
  if (!valid_location(size, *guest_ip, kernel_off, kernel_len))
    return ZX_ERR_IO_DATA_INTEGRITY;

  status = create_boot_params(addr, size, first_page);
  if (status != ZX_OK)
    return status;

  status = load_linux(fd, addr, first_page, kernel_off, kernel_len);
  if (status != ZX_OK)
    return status;

  if (cmdline != nullptr) {
    uintptr_t cmdline_off = kernel_off + kernel_len;
    status = load_cmdline(cmdline, addr, size, first_page, cmdline_off);
    if (status != ZX_OK)
      return status;
  }

  if (initrd_path != nullptr) {
    int initrd_fd = open(initrd_path, O_RDONLY);
    if (initrd_fd < 0) {
      fprintf(stderr, "Failed to open initial RAM disk\n");
      return ZX_ERR_IO;
    }

    status = load_initrd(initrd_fd, addr, size, first_page, kRamdiskOffset);
    close(initrd_fd);
    if (status != ZX_OK)
      return status;
  }

  *ramdisk_off = first_page - addr;
  return ZX_OK;
}
