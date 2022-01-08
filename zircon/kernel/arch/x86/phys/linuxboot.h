// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_LINUXBOOT_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_LINUXBOOT_H_

#include <lib/zircon-internal/e820.h>
#include <stddef.h>
#include <stdint.h>

// This defines some layouts and constants for the Linux/x86 Boot Protocol,
// described in https://www.kernel.org/doc/html/latest/x86/boot.html; this
// file uses the struct and member names in that document, which reflect the
// <asm/bootparam.h> code in the Linux kernel's public headers, but placed in
// the `linuxboot` C++ namespace.  Ony the subset needed by the shim code
// implemented for Zircon is included here.  More can be added as necessary.
namespace linuxboot {

// This is the primary protocol data structure that the boot loader reads and
// writes.  Its placement in boot_params (below) puts the `jump` member at
// exactly 512 bytes from the start of boot_params, which is at the start of
// the whole bzImage file.  The boot loader first loads the first 512-byte
// sector, so it can see just boot_params up through `hdr.boot_flag`.
//
// The boot loader then checks `header`, `version`, and `boot_flag` to validate
// the precise protocol it should be using.  In the versions we support, then
// it consults `loadflags` and `syssize` for more details and those tell it
// which fixed load address to use for the main kernel image, and how big it is
// (in 16-byte units).  As `loadflags` is past the first 512 bytes, the boot
// loader always reads at least one more sector.  Whether it reads more than
// that depends on which booting path it's going to use.  In either path, it
// loads the main kernel at the fixed load address indicated by the version and
// loadflags; for bzImage format it's always 1MiB.
//
//  * Direct 32-bit entry ignores the rest of the setup area.  It allocates a
//    new boot_params struct somewhere and first zeros the whole thing.  Then
//    it copies just the setup_header region from the original image into that
//    boot_params::hdr, and modifies various fields in setup_header and other
//    parts of boot_params to pass information to the kernel.  Finally it
//    simply jumps to the fixed load address indicated by the setup_header
//    flags, in 32-bit protected mode.  The %esi register holds the physical
//    address of the setup area (i.e. `boot_params*`).  The %esp register
//    points to some usable stack space.
//
//  * 16-bit entry uses the rest of the setup area that 32-bit entry ignores.
//    It looks at setup_header::setup_sects for a count of 512-byte sectors to
//    read after the first.  That whole "setup area" from the beginning of the
//    image up through the total size `(hdr.setup_sects + 1) * 512` is loaded
//    at some arbitrary 4KiB-aligned address, hence the moniker "zero page".
//    Then it simply jumps to 512 bytes into the setup area, in 16-bit real
//    mode.  The %cs segment points to this directly, so the entry point is at
//    %cs:0.  The %ds and %es segments point to the start of the setup area, so
//    %ds:0 (i.e. `%ds << 4`) is the boot_params object.  The %ss:%sp points to
//    some usable stack space.  The boot loader fills in a few essential fields
//    but not as much as the 32-bit entry protocol would.  The 16-bit entry
//    code is responsible for discovering more on its own and is expected to
//    rely on the legacy PC 16-bit BIOS ABI.  Traditionally, it allocates a new
//    boot_params struct of its own and copies setup_header and other data into
//    it to pass along to the 32-bit entry point so it looks like the direct
//    32-bit entry from a boot loader would.
//
struct [[gnu::packed]] setup_header {
  enum LoadFlags : uint8_t {
    kLoadedHigh = 1 << 0,  // Load at 1MiB fixed address.
  };

  static constexpr uint16_t kBootFlag = 0xaa55;  // boot_flag must match this.

  uint8_t setup_sects;
  uint16_t root_flags;
  uint32_t syssize;
  uint16_t ram_size;
  uint16_t vid_mode;
  uint16_t root_dev;
  uint16_t boot_flag;
  uint16_t jump;
  uint32_t header;
  uint16_t version;
  uint32_t realmode_swtch;
  uint16_t start_sys_seg;
  uint16_t kernel_version;
  uint8_t type_of_loader;
  uint8_t loadflags;
  uint16_t setup_move_size;
  uint32_t code32_start;
  uint32_t ramdisk_image;
  uint32_t ramdisk_size;
  uint32_t bootsect_kludge;
  uint16_t heap_end_ptr;
  uint8_t ext_loader_ver;
  uint8_t ext_loader_type;
  uint32_t cmd_line_ptr;
  uint32_t initrd_addr_max;
  uint32_t kernel_alignment;
  uint8_t relocatable_kernel;
  uint8_t min_alignment;
  uint16_t xloadflags;
  uint32_t cmdline_size;
  uint32_t hardware_subarch;
  uint64_t hardware_subarch_data;
  uint32_t payload_offset;
  uint32_t payload_length;
  uint64_t setup_data;
  uint64_t pref_address;
  uint32_t init_size;
  uint32_t handover_offset;
  uint32_t kernel_info_offset;
};

static constexpr uint32_t kLoadedHighAddress = 0x100000;

// Many of these inner struct types are not actually consulted by shim code.
// But their layouts are complete here to get the overall boot_params layout.

// This is `struct screen_info` in Linux, but `screen_info` is also used as a
// field name, which isn't good in C++.
struct [[gnu::packed]] screen_info_t {
  uint8_t orig_x;
  uint8_t orig_y;
  uint16_t ext_mem_k;
  uint16_t orig_video_page;
  uint8_t orig_video_mode;
  uint8_t orig_video_cols;
  uint8_t flags;
  uint8_t unused2;
  uint16_t orig_video_ega_bx;
  uint16_t unused3;
  uint8_t orig_video_lines;
  uint8_t orig_video_isVGA;
  uint16_t orig_video_points;
  uint16_t lfb_width;
  uint16_t lfb_height;
  uint16_t lfb_depth;
  uint32_t lfb_base;
  uint32_t lfb_size;
  uint16_t cl_magic, cl_offset;
  uint16_t lfb_linelength;
  uint8_t red_size;
  uint8_t red_pos;
  uint8_t green_size;
  uint8_t green_pos;
  uint8_t blue_size;
  uint8_t blue_pos;
  uint8_t rsvd_size;
  uint8_t rsvd_pos;
  uint16_t vesapm_seg;
  uint16_t vesapm_off;
  uint16_t pages;
  uint16_t vesa_attributes;
  uint32_t capabilities;
  uint32_t ext_lfb_base;
  uint8_t reserved[2];
};

// This is `struct apm_bios_info` in Linux, but `apm_bios_info` is also used as
// a field name, which isn't good in C++.
struct apm_bios_info_t {
  uint16_t version;
  uint16_t cseg;
  uint32_t offset;
  uint16_t cseg_16;
  uint16_t dseg;
  uint16_t flags;
  uint16_t cseg_len;
  uint16_t cseg_16_len;
  uint16_t dseg_len;
};

// This is `struct ist_info` in Linux, but `ist_info` is also used as a field
// name, which isn't good in C++.
struct ist_info_t {
  uint32_t signature;
  uint32_t command;
  uint32_t event;
  uint32_t perf_level;
};

// This is `struct sys_desc_table` in Linux, but `sys_desc_table` is also used
// as a field name, which isn't good in C++.
struct sys_desc_table_t {
  uint16_t length;
  uint8_t table[14];
};

// This is `struct olpc_ofw_header` in Linux, but `olpc_ofw_header` is also
// used as a field name, which isn't good in C++.
struct olpc_ofw_header_t {
  uint32_t ofw_magic; /* OFW signature */
  uint32_t ofw_version;
  uint32_t cif_handler; /* callback into OFW */
  uint32_t irq_desc_table;
};

// This is `struct edid_info` in Linux, but `edid_info` is also
// used as a field name, which isn't good in C++.
using edid_info_t = uint8_t[128];

// This is `struct efi_info` in Linux, but `efi_info` is also
// used as a field name, which isn't good in C++.
struct efi_info_t {
  uint32_t efi_loader_signature;
  uint32_t efi_systab;
  uint32_t efi_memdesc_size;
  uint32_t efi_memdesc_version;
  uint32_t efi_memmap;
  uint32_t efi_memmap_size;
  uint32_t efi_systab_hi;
  uint32_t efi_memmap_hi;
};

struct [[gnu::packed]] edd_device_params {
  uint16_t length;
  uint16_t info_flags;
  uint32_t num_default_cylinders;
  uint32_t num_default_heads;
  uint32_t sectors_per_track;
  uint64_t number_of_sectors;
  uint16_t bytes_per_sector;
  uint32_t dpte_ptr;               /* 0xFFFFFFFF for our purposes */
  uint16_t key;                    /* = 0xBEDD */
  uint8_t device_path_info_length; /* = 44 */
  uint8_t reserved2;
  uint16_t reserved3;
  uint8_t host_bus_type[4];
  uint8_t interface_type[8];
  union {
    struct [[gnu::packed]] {
      uint16_t base_address;
      uint16_t reserved1;
      uint32_t reserved2;
    } isa;
    struct [[gnu::packed]] {
      uint8_t bus;
      uint8_t slot;
      uint8_t function;
      uint8_t channel;
      uint32_t reserved;
    } pci;
    /* pcix is same as pci */
    struct [[gnu::packed]] {
      uint64_t reserved;
    } ibnd;
    struct [[gnu::packed]] {
      uint64_t reserved;
    } xprs;
    struct [[gnu::packed]] {
      uint64_t reserved;
    } htpt;
    struct [[gnu::packed]] {
      uint64_t reserved;
    } unknown;
  } interface_path;
  union {
    struct [[gnu::packed]] {
      uint8_t device;
      uint8_t reserved1;
      uint16_t reserved2;
      uint32_t reserved3;
      uint64_t reserved4;
    } ata;
    struct [[gnu::packed]] {
      uint8_t device;
      uint8_t lun;
      uint8_t reserved1;
      uint8_t reserved2;
      uint32_t reserved3;
      uint64_t reserved4;
    } atapi;
    struct [[gnu::packed]] {
      uint16_t id;
      uint64_t lun;
      uint16_t reserved1;
      uint32_t reserved2;
    } scsi;
    struct [[gnu::packed]] {
      uint64_t serial_number;
      uint64_t reserved;
    } usb;
    struct [[gnu::packed]] {
      uint64_t eui;
      uint64_t reserved;
    } i1394;
    struct [[gnu::packed]] {
      uint64_t wwid;
      uint64_t lun;
    } fibre;
    struct [[gnu::packed]] {
      uint64_t identity_tag;
      uint64_t reserved;
    } i2o;
    struct [[gnu::packed]] {
      uint32_t array_number;
      uint32_t reserved1;
      uint64_t reserved2;
    } raid;
    struct [[gnu::packed]] {
      uint8_t device;
      uint8_t reserved1;
      uint16_t reserved2;
      uint32_t reserved3;
      uint64_t reserved4;
    } sata;
    struct [[gnu::packed]] {
      uint64_t reserved1;
      uint64_t reserved2;
    } unknown;
  } device_path;
  uint8_t reserved4;
  uint8_t checksum;
};

struct [[gnu::packed]] edd_info {
  uint8_t device;
  uint8_t version;
  uint16_t interface_support;
  uint16_t legacy_max_cylinder;
  uint8_t legacy_max_head;
  uint8_t legacy_sectors_per_track;
  edd_device_params params;
};

static constexpr size_t kMaxEddNr = 6;

static constexpr size_t kMaxEddMbrSig = 16;

static constexpr size_t kMaxE820TableEntries = 128;

// This is also known as "the zero page".  This is the overall layout that
// starts the "bzImage" file format.  The `hdr.setup_sects` value determines
// how much is actually loaded along with it.
struct [[gnu::packed]] boot_params {
  screen_info_t screen_info;
  apm_bios_info_t apm_bios_info;
  uint8_t pad2[4];
  uint64_t tboot_addr;
  ist_info_t ist_info;
  uint64_t acpi_rsdp_addr;
  uint8_t _pad3[8];
  uint8_t hd0_info[16];
  uint8_t hd1_info[16];
  sys_desc_table_t sys_desc_table;
  olpc_ofw_header_t olpc_ofw_header;
  uint32_t ext_ramdisk_image;
  uint32_t ext_ramdisk_size;
  uint32_t ext_cmd_line_ptr;
  uint8_t _pad4[116];
  edid_info_t edid_info;
  efi_info_t efi_info;
  uint32_t alt_mem_k;
  uint32_t scratch;
  uint8_t e820_entries;
  uint8_t eddbuf_entries;
  uint8_t edd_mbr_sig_buf_entries;
  uint8_t kbd_status;
  uint8_t secure_boot;
  uint8_t pad5[2];
  uint8_t sentinel;
  uint8_t pad6[1];
  setup_header hdr;
  uint8_t pad7[0x290 - 0x1f1 - sizeof(setup_header)];
  uint32_t edd_mbr_sig_buffer[kMaxEddMbrSig];
  E820Entry e820_table[kMaxE820TableEntries];
  uint8_t pad8[48];
  edd_info eddbuf[kMaxEddNr];
  uint8_t pad9[276];
};

// This is not strictly part of the Linux protocol, but it is used in the
// 16-bit BIOS calls required to populate boot_params in the 16-bit entry path.

constexpr uint32_t kE820Magic = 0x534d4150;  // 'SMAP'

}  // namespace linuxboot

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_LINUXBOOT_H_
