// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <libfdt.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <hypervisor/bits.h>
#include <hypervisor/guest.h>

#include "garnet/bin/guest/efi.h"
#include "garnet/bin/guest/kernel.h"
#include "garnet/bin/guest/linux.h"

#if __x86_64__
#include "garnet/lib/machina/arch/x86/e820.h"
#endif

static const uint8_t kLoaderTypeUnspecified = 0xff;  // Unknown bootloader
static const uint16_t kMinBootProtocol = 0x200;      // bzImage boot protocol
static const uint16_t kBootFlagMagic = 0xaa55;
static const uint32_t kHeaderMagic = 0x53726448;
static const uintptr_t kEntryOffset = 0x200;
__UNUSED static const uintptr_t kE820MapOffset = 0x02d0;
__UNUSED static const size_t kMaxE820Entries = 128;
__UNUSED static const size_t kSectorSize = 512;

static const char kDtbPath[] = "/pkg/data/board.dtb";

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

static zx_status_t read_fd(const uintptr_t addr,
                           const size_t size,
                           const int fd,
                           const uintptr_t off,
                           size_t* file_size) {
  struct stat stat;
  ssize_t ret = fstat(fd, &stat);
  if (ret < 0) {
    fprintf(stderr, "Failed to stat file\n");
    return ZX_ERR_IO;
  }
  if (stat.st_size > size - off) {
    fprintf(stderr, "File is too large\n");
    return ZX_ERR_OUT_OF_RANGE;
  }
  off_t fd_off = lseek(fd, 0, SEEK_CUR);
  if (fd_off < 0) {
    fprintf(stderr, "Failed to get file position\n");
    return ZX_ERR_IO;
  }
  ret = read(fd, reinterpret_cast<void*>(addr + off), stat.st_size);
  if (ret != stat.st_size - fd_off) {
    fprintf(stderr, "Failed to read file\n");
    return ZX_ERR_IO;
  }
  if (file_size != nullptr)
    *file_size = stat.st_size;
  return ZX_OK;
}

static zx_status_t read_boot_params(const uintptr_t first_page,
                                    const int fd,
                                    uintptr_t* guest_ip,
                                    uintptr_t* kernel_off,
                                    size_t* kernel_len) {
  // Validate kernel configuration.
  uint16_t xloadflags = bp(first_page, XLOADFLAGS);
  if (~xloadflags & (KERNEL_64 | CAN_BE_LOADED_ABOVE_4G)) {
    fprintf(stderr, "Unsupported Linux kernel\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint16_t protocol = bp(first_page, VERSION);
  uint8_t loadflags = bp(first_page, LOADFLAGS);
  if (protocol < kMinBootProtocol || !(loadflags & LOAD_HIGH)) {
    fprintf(stderr, "Linux kernel is not a bzImage\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Seek to the protected mode kernel.
  uint8_t setup_sects = bp(first_page, SETUP_SECTS);
  if (setup_sects == 0) {
    // 0 here actually means 4, see boot.txt.
    setup_sects = 4;
  }
  off_t seek_off = (setup_sects + 1) * kSectorSize;
  off_t ret = lseek(fd, seek_off, SEEK_SET);
  if (ret < 0) {
    fprintf(stderr, "Failed seek to protected mode Linux kernel\n");
    return ZX_ERR_IO;
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

static zx_status_t write_boot_params(const uintptr_t addr,
                                     const size_t size,
                                     const uintptr_t first_page,
                                     const char* cmdline,
                                     const uintptr_t cmdline_off,
                                     const size_t initrd_size) {
  // Set type of bootloader.
  bp(first_page, LOADER_TYPE) = kLoaderTypeUnspecified;

  // Zero video, columns and lines to skip early video init.
  bp(first_page, VIDEO_MODE) = 0;
  bp(first_page, VIDEO_COLS) = 0;
  bp(first_page, VIDEO_LINES) = 0;

  // Set the address and size of the initial RAM disk.
  bp(first_page, RAMDISK_IMAGE) = kRamdiskOffset;
  bp(first_page, RAMDISK_SIZE) = static_cast<uint32_t>(initrd_size);

  // Copy the command line string.
  size_t cmdline_len = strlen(cmdline) + 1;
  if (cmdline_off > UINT32_MAX || cmdline_len > size - cmdline_off) {
    fprintf(stderr, "Command line is too long\n");
    return ZX_ERR_OUT_OF_RANGE;
  }
  memcpy(reinterpret_cast<void*>(addr + cmdline_off), cmdline, cmdline_len);
  bp(first_page, COMMAND_LINE) = static_cast<uint32_t>(cmdline_off);

#if __aarch64__
  return ZX_OK;
#elif __x86_64__
  // Setup e820 memory map.
  size_t e820_entries = machina::e820_entries(size);
  if (e820_entries > kMaxE820Entries) {
    fprintf(stderr, "Not enough space for e820 memory map\n");
    return ZX_ERR_BAD_STATE;
  }
  bp(first_page, E820_COUNT) = static_cast<uint8_t>(e820_entries);

  return machina::create_e820(addr, size, first_page - addr + kE820MapOffset);
#endif
}

static zx_status_t load_device_tree(const uintptr_t addr,
                                    const size_t size,
                                    const int fd,
                                    const char* cmdline,
                                    const size_t initrd_size,
                                    const uintptr_t dtb_off) {
  size_t dtb_size = 0;
  zx_status_t status = read_fd(addr, size, fd, dtb_off, &dtb_size);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to load device tree\n");
    return status;
  }

  // Validate device tree.
  void* dtb = reinterpret_cast<void*>(addr + dtb_off);
  int ret = fdt_check_header(dtb);
  if (ret < 0) {
    fprintf(stderr, "Invalid device tree\n");
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int off = fdt_path_offset(dtb, "/chosen");
  if (off < 0) {
    fprintf(stderr, "Failed to find \"/chosen\" in device tree\n");
    return ZX_ERR_BAD_STATE;
  }
  // Add command line to device tree.
  ret = fdt_setprop_string(dtb, off, "bootargs", cmdline);
  if (ret < 0) {
    fprintf(stderr,
            "Failed to add \"bootargs\" property to device tree, space must be "
            "reserved in the device tree\n");
    return ZX_ERR_BAD_STATE;
  }
  // Add the memory range of the initial RAM disk.
  ret = fdt_setprop_u64(dtb, off, "linux,initrd-start", kRamdiskOffset);
  if (ret < 0) {
    fprintf(stderr,
            "Failed to add \"linux,initrd-start\" property to device tree, "
            "space must be reserved in the device tree\n");
    return ZX_ERR_BAD_STATE;
  }
  ret = fdt_setprop_u64(dtb, off, "linux,initrd-end",
                        kRamdiskOffset + initrd_size);
  if (ret < 0) {
    fprintf(stderr,
            "Failed to add \"linux,initrd-end\" property to device tree, space "
            "must be reserved in the device tree\n");
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t setup_linux(const uintptr_t addr,
                        const size_t size,
                        const uintptr_t first_page,
                        const int fd,
                        const char* initrd_path,
                        const char* cmdline,
                        uintptr_t* guest_ip,
                        uintptr_t* boot_ptr) {
  size_t initrd_size = 0;
  if (initrd_path != nullptr) {
    fbl::unique_fd initrd_fd(open(initrd_path, O_RDONLY));
    if (!initrd_fd) {
      fprintf(stderr, "Failed to open initial RAM disk\n");
      return ZX_ERR_IO;
    }

    zx_status_t status =
        read_fd(addr, size, initrd_fd.get(), kRamdiskOffset, &initrd_size);
    if (status != ZX_OK) {
      fprintf(stderr, "Failed to read initial RAM disk\n");
      return status;
    }
  }

  uintptr_t kernel_off = 0;
  size_t kernel_len = 0;
  if (is_boot_params(first_page)) {
    zx_status_t status =
        read_boot_params(first_page, fd, guest_ip, &kernel_off, &kernel_len);
    if (status != ZX_OK)
      return status;

    uintptr_t cmdline_off = kernel_off + kernel_len;
    status = write_boot_params(addr, size, first_page, cmdline, cmdline_off,
                               initrd_size);
    if (status != ZX_OK)
      return status;
  } else {
    zx_status_t status =
        read_efi(first_page, guest_ip, &kernel_off, &kernel_len);
    if (status != ZX_OK)
      return status;

    fbl::unique_fd dtb_fd(open(kDtbPath, O_RDONLY));
    if (!dtb_fd) {
      fprintf(stderr, "Failed to open device tree\n");
      return ZX_ERR_IO;
    }

    status = load_device_tree(addr, size, dtb_fd.get(), cmdline, initrd_size,
                              first_page - addr);
    if (status != ZX_OK)
      return status;
  }

  if (!valid_location(size, *guest_ip, kernel_off, kernel_len))
    return ZX_ERR_IO_DATA_INTEGRITY;

  zx_status_t status = read_fd(addr, size, fd, kernel_off, nullptr);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to read Linux kernel\n");
    return status;
  }

  *boot_ptr = first_page - addr;
  return ZX_OK;
}
