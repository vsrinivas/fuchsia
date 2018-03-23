// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/linux.h"

#include <fcntl.h>
#include <inttypes.h>
#include <libfdt.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>

#include "garnet/bin/guest/kernel.h"
#include "garnet/lib/machina/bits.h"
#include "garnet/lib/machina/guest.h"
#include "lib/fxl/strings/string_printf.h"

#if __aarch64__
static constexpr uintptr_t kKernelOffset = 0x80000;
#elif __x86_64__
static constexpr uintptr_t kKernelOffset = 0x200000;
#include "garnet/lib/machina/arch/x86/acpi.h"
#include "garnet/lib/machina/arch/x86/e820.h"
#endif

static constexpr uint8_t kLoaderTypeUnspecified = 0xff;  // Unknown bootloader
static constexpr uint16_t kMinBootProtocol = 0x200;  // bzImage boot protocol
static constexpr uint16_t kBootFlagMagic = 0xaa55;
static constexpr uint32_t kHeaderMagic = 0x53726448;
static constexpr uintptr_t kEntryOffset = 0x200;
__UNUSED static constexpr uintptr_t kE820MapOffset = 0x02d0;
__UNUSED static constexpr size_t kMaxE820Entries = 128;
__UNUSED static constexpr size_t kSectorSize = 512;

static constexpr uint16_t kMzSignature = 0x5a4d;  // MZ
static constexpr uint32_t kMzMagic = 0x644d5241;  // ARM\x64

static constexpr char kDtbPath[] = "/pkg/data/board.dtb";
static constexpr uintptr_t kDtbOffset = kRamdiskOffset - PAGE_SIZE;

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

enum Lf : uint8_t {
  LOAD_HIGH                 = 1u << 0,  // Protected mode code loads at 0x100000
};

enum Xlf : uint16_t {
  KERNEL_64                 = 1u << 0,  // Has legacy 64-bit entry point at 0x200
  CAN_BE_LOADED_ABOVE_4G    = 1u << 1,  // Kernel/boot_params/cmdline/ramdisk can be above 4G
};

// clang-format on

static uint8_t& bp(const machina::PhysMem& phys_mem, Bp8 off) {
  return *phys_mem.as<uint8_t>(kKernelOffset + off);
}

static uint16_t& bp(const machina::PhysMem& phys_mem, Bp16 off) {
  return *phys_mem.as<uint16_t>(kKernelOffset + off);
}

static uint32_t& bp(const machina::PhysMem& phys_mem, Bp32 off) {
  return *phys_mem.as<uint32_t>(kKernelOffset + off);
}

static bool is_boot_params(const machina::PhysMem& phys_mem) {
  return bp(phys_mem, BOOTFLAG) == kBootFlagMagic &&
         bp(phys_mem, HEADER) == kHeaderMagic;
}

// MZ header used to boot ARM64 kernels.
//
// See: https://www.kernel.org/doc/Documentation/arm64/booting.txt.
struct MzHeader {
  uint32_t code0;
  uint32_t code1;
  uint64_t kernel_off;
  uint64_t kernel_len;
  uint64_t flags;
  uint64_t reserved0;
  uint64_t reserved1;
  uint64_t reserved2;
  uint32_t magic;
  uint32_t pe_off;
} __PACKED;
static_assert(sizeof(MzHeader) == 64, "");

static bool is_mz(const MzHeader* header) {
  return (header->code0 & UINT16_MAX) == kMzSignature &&
         header->kernel_len > sizeof(MzHeader) && header->magic == kMzMagic &&
         header->pe_off >= sizeof(MzHeader);
}

static zx_status_t read_fd(const int fd,
                           const machina::PhysMem& phys_mem,
                           const uintptr_t off,
                           size_t* file_size) {
  struct stat stat;
  ssize_t ret = fstat(fd, &stat);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to stat file";
    return ZX_ERR_IO;
  }
  ret = read(fd, phys_mem.ptr(off, stat.st_size), stat.st_size);
  if (ret != stat.st_size) {
    FXL_LOG(ERROR) << "Failed to read file";
    return ZX_ERR_IO;
  }
  *file_size = stat.st_size;
  return ZX_OK;
}

static zx_status_t read_boot_params(const machina::PhysMem& phys_mem,
                                    uintptr_t* guest_ip) {
  // Validate kernel configuration.
  uint16_t xloadflags = bp(phys_mem, XLOADFLAGS);
  if (~xloadflags & (KERNEL_64 | CAN_BE_LOADED_ABOVE_4G)) {
    FXL_LOG(ERROR) << "Unsupported Linux kernel";
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint16_t protocol = bp(phys_mem, VERSION);
  uint8_t loadflags = bp(phys_mem, LOADFLAGS);
  if (protocol < kMinBootProtocol || !(loadflags & LOAD_HIGH)) {
    FXL_LOG(ERROR) << "Linux kernel is not a bzImage";
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (bp(phys_mem, RELOCATABLE) == 0) {
    FXL_LOG(ERROR) << "Linux kernel is not relocatable";
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint64_t kernel_align = bp(phys_mem, KERNEL_ALIGN);
  if (kKernelOffset % kernel_align != 0) {
    FXL_LOG(ERROR) << "Linux kernel has unsupported alignment";
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Calculate the offset to the protected mode kernel.
  uint8_t setup_sects = bp(phys_mem, SETUP_SECTS);
  if (setup_sects == 0) {
    // 0 here actually means 4, see boot.txt.
    setup_sects = 4;
  }
  uintptr_t setup_off = (setup_sects + 1) * kSectorSize;
  *guest_ip = kKernelOffset + kEntryOffset + setup_off;
  return ZX_OK;
}

static zx_status_t write_boot_params(const machina::PhysMem& phys_mem,
                                     const std::string& cmdline,
                                     const size_t initrd_size) {
  // Set type of bootloader.
  bp(phys_mem, LOADER_TYPE) = kLoaderTypeUnspecified;

  // Zero video, columns and lines to skip early video init.
  bp(phys_mem, VIDEO_MODE) = 0;
  bp(phys_mem, VIDEO_COLS) = 0;
  bp(phys_mem, VIDEO_LINES) = 0;

  // Set the address and size of the initial RAM disk.
  bp(phys_mem, RAMDISK_IMAGE) = kRamdiskOffset;
  bp(phys_mem, RAMDISK_SIZE) = static_cast<uint32_t>(initrd_size);

  // Copy the command line string.
  size_t cmdline_len = cmdline.size() + 1;
  if (phys_mem.size() < PAGE_SIZE || cmdline_len > PAGE_SIZE) {
    FXL_LOG(ERROR) << "Command line is too long";
    return ZX_ERR_OUT_OF_RANGE;
  }
  uint32_t cmdline_off = phys_mem.size() - PAGE_SIZE;
  memcpy(phys_mem.ptr(cmdline_off, cmdline_len), cmdline.c_str(), cmdline_len);
  bp(phys_mem, COMMAND_LINE) = cmdline_off;

#if __aarch64__
  return ZX_OK;
#elif __x86_64__
  // Setup e820 memory map.
  size_t e820_entries = machina::e820_entries(phys_mem.size());
  if (e820_entries > kMaxE820Entries) {
    FXL_LOG(ERROR) << "Not enough space for e820 memory map";
    return ZX_ERR_BAD_STATE;
  }
  bp(phys_mem, E820_COUNT) = static_cast<uint8_t>(e820_entries);

  return machina::create_e820(phys_mem, kKernelOffset + kE820MapOffset);
#endif
}

static zx_status_t read_mz(const machina::PhysMem& phys_mem,
                           uintptr_t* guest_ip) {
  MzHeader* mz_header = phys_mem.as<MzHeader>(kKernelOffset);
  if (!is_mz(mz_header))
    return ZX_ERR_NOT_SUPPORTED;

  *guest_ip = kKernelOffset;
  return ZX_OK;
}

static zx_status_t load_device_tree(const int fd,
                                    const machina::PhysMem& phys_mem,
                                    const std::string& cmdline,
                                    const size_t initrd_size) {
  size_t dtb_size;
  zx_status_t status = read_fd(fd, phys_mem, kDtbOffset, &dtb_size);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load device tree";
    return status;
  }
  if (kDtbOffset + dtb_size > kRamdiskOffset) {
    FXL_LOG(ERROR) << "DTB is too large";
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Validate device tree.
  void* dtb = phys_mem.ptr(kDtbOffset, dtb_size);
  int ret = fdt_check_header(dtb);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int off = fdt_path_offset(dtb, "/chosen");
  if (off < 0) {
    FXL_LOG(ERROR) << "Failed to find \"/chosen\" in device tree";
    return ZX_ERR_BAD_STATE;
  }
  // Add command line to device tree.
  ret = fdt_setprop_string(dtb, off, "bootargs", cmdline.c_str());
  if (ret < 0) {
    FXL_LOG(ERROR)
        << "Failed to add \"bootargs\" property to device tree, space must be "
           "reserved in the device tree";
    return ZX_ERR_BAD_STATE;
  }
  // Add the memory range of the initial RAM disk.
  ret = fdt_setprop_u64(dtb, off, "linux,initrd-start", kRamdiskOffset);
  if (ret < 0) {
    FXL_LOG(ERROR)
        << "Failed to add \"linux,initrd-start\" property to device tree, "
           "space must be reserved in the device tree";
    return ZX_ERR_BAD_STATE;
  }
  ret = fdt_setprop_u64(dtb, off, "linux,initrd-end",
                        kRamdiskOffset + initrd_size);
  if (ret < 0) {
    FXL_LOG(ERROR)
        << "Failed to add \"linux,initrd-end\" property to device tree, space "
           "must be reserved in the device tree";
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

static std::string linux_cmdline(std::string cmdline) {
#if __x86_64__
  fxl::StringAppendf(&cmdline, " acpi_rsdp=%#lx", machina::kAcpiOffset);
#endif
  return cmdline;
}

zx_status_t setup_linux(const GuestConfig cfg,
                        const machina::PhysMem& phys_mem,
                        uintptr_t* guest_ip,
                        uintptr_t* boot_ptr) {
  // Read the kernel image.
  zx_status_t status = load_kernel(cfg.kernel_path(), phys_mem, kKernelOffset);
  if (status != ZX_OK) {
    return status;
  }

  size_t initrd_size = 0;
  if (!cfg.ramdisk_path().empty()) {
    fbl::unique_fd initrd_fd(open(cfg.ramdisk_path().c_str(), O_RDONLY));
    if (!initrd_fd) {
      FXL_LOG(ERROR) << "Failed to open initial RAM disk "
                     << cfg.ramdisk_path();
      return ZX_ERR_IO;
    }

    status = read_fd(initrd_fd.get(), phys_mem, kRamdiskOffset, &initrd_size);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to read initial RAM disk "
                     << cfg.ramdisk_path();
      return status;
    }
  }

  std::string cmdline = linux_cmdline(cfg.cmdline());
  if (is_boot_params(phys_mem)) {
    status = read_boot_params(phys_mem, guest_ip);
    if (status != ZX_OK) {
      return status;
    }
    status = write_boot_params(phys_mem, cmdline, initrd_size);
    if (status != ZX_OK) {
      return status;
    }
    *boot_ptr = kKernelOffset;
  } else {
    status = read_mz(phys_mem, guest_ip);
    if (status != ZX_OK) {
      return status;
    }
    fbl::unique_fd dtb_fd(open(kDtbPath, O_RDONLY));
    if (!dtb_fd) {
      FXL_LOG(ERROR) << "Failed to open device tree " << kDtbPath;
      return ZX_ERR_IO;
    }
    status = load_device_tree(dtb_fd.get(), phys_mem, cmdline, initrd_size);
    if (status != ZX_OK) {
      return status;
    }
    *boot_ptr = kDtbOffset;
  }

  return ZX_OK;
}
