// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>

#include <arch/mmu.h>
#include <arch/mp.h>
#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/mmu.h>

#include "platform_p.h"
#if defined(WITH_KERNEL_PCIE)
#include <dev/pcie_bus_driver.h>
#endif
#include <err.h>
#include <lib/acpi_tables.h>
#include <lib/cksum.h>
#include <lib/cmdline.h>
#include <lib/debuglog.h>
#include <lib/system-topology.h>
#include <mexec.h>
#include <platform.h>
#include <string.h>
#include <trace.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/image.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <dev/uart.h>
#include <explicit-memory/bytes.h>
#include <fbl/alloc_checker.h>
#include <fbl/vector.h>
#include <libzbi/zbi-cpp.h>
#include <lk/init.h>
#include <platform/console.h>
#include <platform/keyboard.h>
#include <platform/pc.h>
#include <platform/pc/bootloader.h>
#include <platform/pc/smbios.h>
#include <vm/bootalloc.h>
#include <vm/bootreserve.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>

extern "C" {
#include <efi/runtime-services.h>
#include <efi/system-table.h>
}

#define LOCAL_TRACE 0

extern zbi_header_t* _zbi_base;

pc_bootloader_info_t bootloader;

// Stashed values from ZBI_TYPE_CRASHLOG if we saw it
static const void* last_crashlog = nullptr;
static size_t last_crashlog_len = 0;

// convert from legacy format
static unsigned pixel_format_fixup(unsigned pf) {
  switch (pf) {
    case 1:
      return ZX_PIXEL_FORMAT_RGB_565;
    case 2:
      return ZX_PIXEL_FORMAT_RGB_332;
    case 3:
      return ZX_PIXEL_FORMAT_RGB_2220;
    case 4:
      return ZX_PIXEL_FORMAT_ARGB_8888;
    case 5:
      return ZX_PIXEL_FORMAT_RGB_x888;
    default:
      return pf;
  }
}

static bool early_console_disabled;

zbi_result_t process_zbi_item(zbi_header_t* hdr, void* payload, void* cookie) {
  switch (hdr->type) {
    case ZBI_TYPE_PLATFORM_ID:
      if (hdr->length >= sizeof(zbi_platform_id_t)) {
        memcpy(&bootloader.platform_id, payload, sizeof(zbi_platform_id_t));
        bootloader.platform_id_size = sizeof(zbi_platform_id_t);
      }
      break;
    case ZBI_TYPE_ACPI_RSDP:
      if (hdr->length >= sizeof(uint64_t)) {
        bootloader.acpi_rsdp = *((uint64_t*)payload);
      }
      break;
    case ZBI_TYPE_SMBIOS:
      if (hdr->length >= sizeof(uint64_t)) {
        bootloader.smbios = *((uint64_t*)payload);
      }
      break;
    case ZBI_TYPE_EFI_SYSTEM_TABLE:
      if (hdr->length >= sizeof(uint64_t)) {
        bootloader.efi_system_table = (void*)*((uint64_t*)payload);
      }
      break;
    case ZBI_TYPE_FRAMEBUFFER: {
      if (hdr->length >= sizeof(zbi_swfb_t)) {
        memcpy(&bootloader.fb, payload, sizeof(zbi_swfb_t));
      }
      bootloader.fb.format = pixel_format_fixup(bootloader.fb.format);
      break;
    }
    case ZBI_TYPE_CMDLINE:
      if (hdr->length > 0) {
        ((char*)payload)[hdr->length - 1] = 0;
        gCmdline.Append((char*)payload);

        // The CMDLINE might include entropy for the zircon cprng.
        // We don't want that information to be accesible after it has
        // been added to the kernel cmdline.
        mandatory_memset(payload, 0, hdr->length);
        hdr->type = ZBI_TYPE_DISCARD;
        hdr->crc32 = ZBI_ITEM_NO_CRC32;
        hdr->flags &= ~ZBI_FLAG_CRC32;
      }
      break;
    case ZBI_TYPE_EFI_MEMORY_MAP:
      bootloader.efi_mmap = payload;
      bootloader.efi_mmap_size = hdr->length;
      break;
    case ZBI_TYPE_E820_TABLE:
      bootloader.e820_table = payload;
      bootloader.e820_count = hdr->length / sizeof(e820entry_t);
      break;
    case ZBI_TYPE_NVRAM_DEPRECATED:
    // fallthrough: this is a legacy/typo variant
    case ZBI_TYPE_NVRAM:
      if (hdr->length >= sizeof(zbi_nvram_t)) {
        memcpy(&bootloader.nvram, payload, sizeof(zbi_nvram_t));
      }
      break;
    case ZBI_TYPE_DEBUG_UART:
      if (hdr->length >= sizeof(zbi_uart_t)) {
        memcpy(&bootloader.uart, payload, sizeof(zbi_uart_t));
      }
      break;
    case ZBI_TYPE_CRASHLOG:
      last_crashlog = payload;
      last_crashlog_len = hdr->length;
      break;
    case ZBI_TYPE_DISCARD:
      break;
  }
  return ZBI_RESULT_OK;
}

static void process_zbi(zbi_header_t* hdr, uintptr_t phys) {
  uint8_t* zbi_base = reinterpret_cast<uint8_t*>(hdr);

  zbi::Zbi image(zbi_base);

  // Make sure the image is in good shape.
  zbi_header_t* bad_hdr;
  zbi_result_t result = image.Check(&bad_hdr);
  if (result != ZBI_RESULT_OK) {
    printf("zbi: invalid %08x %08x %08x %08x, retcode = %d\n", bad_hdr->type, bad_hdr->length,
           bad_hdr->extra, bad_hdr->flags, result);
    return;
  }

  printf("zbi: @ %p (%u bytes)\n", image.Base(), image.Length());

  result = image.ForEach(process_zbi_item, nullptr);
  if (result != ZBI_RESULT_OK) {
    printf("zbi: failed to process bootdata, reason = %d\n", result);
    return;
  }

  boot_alloc_reserve(phys, image.Length());
  bootloader.ramdisk_base = phys;
  bootloader.ramdisk_size = image.Length();
}

extern bool halt_on_panic;

static void platform_save_bootloader_data(void) {
  if (_zbi_base != NULL) {
    zbi_header_t* bd = (zbi_header_t*)X86_PHYS_TO_VIRT(_zbi_base);
    process_zbi(bd, (uintptr_t)_zbi_base);
  }

  halt_on_panic = gCmdline.GetBool("kernel.halt-on-panic", false);
}

static void* ramdisk_base;
static size_t ramdisk_size;

static void platform_preserve_ramdisk(void) {
  if (bootloader.ramdisk_size == 0) {
    return;
  }
  if (bootloader.ramdisk_base == 0) {
    return;
  }

  size_t pages = ROUNDUP_PAGE_SIZE(bootloader.ramdisk_size) / PAGE_SIZE;
  ramdisk_base = paddr_to_physmap(bootloader.ramdisk_base);
  ramdisk_size = pages * PAGE_SIZE;

  // add the ramdisk to the boot reserve list
  boot_reserve_add_range(bootloader.ramdisk_base, ramdisk_size);
}

void* platform_get_ramdisk(size_t* size) {
  if (ramdisk_base) {
    *size = ramdisk_size;
    return ramdisk_base;
  } else {
    *size = 0;
    return NULL;
  }
}

#include <lib/gfxconsole.h>

#include <dev/display.h>

zx_status_t display_get_info(struct display_info* info) {
  return gfxconsole_display_get_info(info);
}

bool platform_early_console_enabled() { return !early_console_disabled; }

static void platform_early_display_init(void) {
  struct display_info info;
  void* bits;

  if (bootloader.fb.base == 0) {
    return;
  }

  if (gCmdline.GetBool("gfxconsole.early", false) == false) {
    early_console_disabled = true;
    return;
  }

  // allocate an offscreen buffer of worst-case size, page aligned
  bits = boot_alloc_mem(8192 + bootloader.fb.height * bootloader.fb.stride * 4);
  bits = (void*)((((uintptr_t)bits) + 4095) & (~4095));

  memset(&info, 0, sizeof(info));
  info.format = bootloader.fb.format;
  info.width = bootloader.fb.width;
  info.height = bootloader.fb.height;
  info.stride = bootloader.fb.stride;
  info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;
  info.framebuffer = (void*)X86_PHYS_TO_VIRT(bootloader.fb.base);

  gfxconsole_bind_display(&info, bits);
}

/* Ensure the framebuffer is write-combining as soon as we have the VMM.
 * Some system firmware has the MTRRs for the framebuffer set to Uncached.
 * Since dealing with MTRRs is rather complicated, we wait for the VMM to
 * come up so we can use PAT to manage the memory types. */
static void platform_ensure_display_memtype(uint level) {
  if (bootloader.fb.base == 0) {
    return;
  }
  if (early_console_disabled) {
    return;
  }
  struct display_info info;
  memset(&info, 0, sizeof(info));
  info.format = bootloader.fb.format;
  info.width = bootloader.fb.width;
  info.height = bootloader.fb.height;
  info.stride = bootloader.fb.stride;
  info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;

  void* addr = NULL;
  zx_status_t status = VmAspace::kernel_aspace()->AllocPhysical(
      "boot_fb", ROUNDUP(info.stride * info.height * 4, PAGE_SIZE), &addr, PAGE_SIZE_SHIFT,
      bootloader.fb.base, 0 /* vmm flags */,
      ARCH_MMU_FLAG_WRITE_COMBINING | ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
  if (status != ZX_OK) {
    TRACEF("Failed to map boot_fb: %d\n", status);
    return;
  }

  info.framebuffer = addr;
  gfxconsole_bind_display(&info, NULL);
}
LK_INIT_HOOK(display_memtype, &platform_ensure_display_memtype, LK_INIT_LEVEL_VM + 1)

static efi_guid zircon_guid = ZIRCON_VENDOR_GUID;
static char16_t crashlog_name[] = ZIRCON_CRASHLOG_EFIVAR

    static fbl::RefPtr<VmAspace>
        efi_aspace;

typedef struct {
  uint64_t magic;
  uint64_t length;
  uint64_t nmagic;
  uint64_t nlength;
} log_hdr_t;

#define NVRAM_MAGIC (0x6f8962d66b28504fULL)

static size_t nvram_stow_crashlog(void* log, size_t len) {
  size_t max = bootloader.nvram.length - sizeof(log_hdr_t);
  void* nvram = paddr_to_physmap(bootloader.nvram.base);
  if (nvram == NULL) {
    return 0;
  }

  if (log == NULL) {
    return max;
  }
  if (len > max) {
    len = max;
  }

  log_hdr_t hdr = {
      .magic = NVRAM_MAGIC,
      .length = len,
      .nmagic = ~NVRAM_MAGIC,
      .nlength = ~len,
  };
  memcpy(nvram, &hdr, sizeof(hdr));
  memcpy(static_cast<char*>(nvram) + sizeof(hdr), log, len);
  arch_clean_cache_range((uintptr_t)nvram, sizeof(hdr) + len);
  return len;
}

static size_t nvram_recover_crashlog(size_t len, void* cookie,
                                     void (*func)(const void* data, size_t, size_t len,
                                                  void* cookie)) {
  size_t max = bootloader.nvram.length - sizeof(log_hdr_t);
  void* nvram = paddr_to_physmap(bootloader.nvram.base);
  if (nvram == NULL) {
    return 0;
  }
  log_hdr_t hdr;
  memcpy(&hdr, nvram, sizeof(hdr));
  if ((hdr.magic != NVRAM_MAGIC) || (hdr.length > max) || (hdr.nmagic != ~NVRAM_MAGIC) ||
      (hdr.nlength != ~hdr.length)) {
    printf("nvram-crashlog: bad header: %016lx %016lx %016lx %016lx\n", hdr.magic, hdr.length,
           hdr.nmagic, hdr.nlength);
    return 0;
  }
  if (len == 0) {
    return hdr.length;
  }
  if (len > hdr.length) {
    len = hdr.length;
  }
  func(static_cast<char*>(nvram) + sizeof(hdr), 0, len, cookie);

  // invalidate header so we don't get a stale crashlog
  // on future boots
  hdr.magic = 0;
  memcpy(nvram, &hdr, sizeof(hdr));
  return hdr.length;
}

void platform_init_crashlog(void) {
  if (bootloader.nvram.base && bootloader.nvram.length > sizeof(log_hdr_t)) {
    // Nothing to do for simple nvram logs
    return;
  } else {
    bootloader.nvram.base = 0;
    bootloader.nvram.length = 0;
  }

  if (bootloader.efi_system_table != NULL) {
    // Create a linear mapping to use to call UEFI Runtime Services
    efi_aspace = VmAspace::Create(VmAspace::TYPE_LOW_KERNEL, "uefi");
    if (!efi_aspace) {
      return;
    }

    // TODO: get more precise about this.  This gets the job done on
    //      the platforms we're working on right now, but is probably
    //      not entirely correct.
    void* ptr = (void*)0;
    zx_status_t r = efi_aspace->AllocPhysical(
        "1:1", 16 * 1024 * 1024 * 1024UL, &ptr, PAGE_SIZE_SHIFT, 0,
        VmAspace::VMM_FLAG_VALLOC_SPECIFIC,
        ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE);

    if (r != ZX_OK) {
      efi_aspace.reset();
    }
  }
}

// Something big enough for the panic log but not too enormous
// to avoid excessive pressure on efi variable storage
#define MAX_EFI_CRASHLOG_LEN 4096

static size_t efi_stow_crashlog(void* log, size_t len) {
  if (!efi_aspace) {
    return 0;
  }
  if (log == NULL) {
    return MAX_EFI_CRASHLOG_LEN;
  }
  if (len > MAX_EFI_CRASHLOG_LEN) {
    len = MAX_EFI_CRASHLOG_LEN;
  }

  // We could be panicking whilst already holding the thread_lock. If so we must avoid calling
  // functions that will grab it again.
  if (spin_lock_held(&thread_lock)) {
    vmm_set_active_aspace_locked(reinterpret_cast<vmm_aspace_t*>(efi_aspace.get()));
  } else {
    vmm_set_active_aspace(reinterpret_cast<vmm_aspace_t*>(efi_aspace.get()));
  }

  efi_system_table* sys = static_cast<efi_system_table*>(bootloader.efi_system_table);
  efi_runtime_services* rs = sys->RuntimeServices;
  if (rs->SetVariable(crashlog_name, &zircon_guid, ZIRCON_CRASHLOG_EFIATTR, len, log) == 0) {
    return len;
  } else {
    return 0;
  }
}

size_t platform_stow_crashlog(void* log, size_t len) {
  printf("stowing crashlog:\n");
  hexdump(log, MIN(64u, len));
  printf("...\n");

  if (bootloader.nvram.base) {
    return nvram_stow_crashlog(log, len);
  } else {
    return efi_stow_crashlog(log, len);
  }
}

size_t platform_recover_crashlog(size_t len, void* cookie,
                                 void (*func)(const void* data, size_t, size_t len, void* cookie)) {
  if (bootloader.nvram.base != 0) {
    return nvram_recover_crashlog(len, cookie, func);
  } else if (last_crashlog != nullptr) {
    if (len != 0) {
      func(last_crashlog, 0, last_crashlog_len, cookie);
    }
    return last_crashlog_len;
  }
  return 0;
}

typedef struct e820_walk_ctx {
  uint8_t* buf;
  size_t len;
  zx_status_t ret;
} e820_walk_ctx_t;

static void e820_entry_walk(uint64_t base, uint64_t size, bool is_mem, void* void_ctx) {
  e820_walk_ctx* ctx = (e820_walk_ctx*)void_ctx;

  // Something went wrong in one of the previous calls, don't attempt to
  // continue.
  if (ctx->ret != ZX_OK)
    return;

  // Make sure we have enough space in the buffer.
  if (ctx->len < sizeof(e820entry_t)) {
    ctx->ret = ZX_ERR_BUFFER_TOO_SMALL;
    return;
  }

  e820entry_t* entry = (e820entry_t*)ctx->buf;
  entry->addr = base;
  entry->size = size;

  // Hack: When we first parse this map we normalize each section to either
  // memory or not-memory. When we pass it to the next kernel, we lose
  // information about the type of "not memory" in each region.
  entry->type = is_mem ? E820_RAM : E820_RESERVED;

  ctx->buf += sizeof(*entry);
  ctx->len -= sizeof(*entry);
  ctx->ret = ZX_OK;
}

// Give the platform an opportunity to append any platform specific bootdata
// sections.
zx_status_t platform_mexec_patch_zbi(uint8_t* bootdata, const size_t len) {
  uint8_t e820buf[sizeof(e820entry_t) * 64];

  e820_walk_ctx ctx;
  ctx.buf = e820buf;
  ctx.len = sizeof(e820buf);
  ctx.ret = ZX_OK;

  zx_status_t ret = enumerate_e820(e820_entry_walk, &ctx);

  if (ret != ZX_OK) {
    printf("mexec: enumerate_e820 failed. Retcode = %d\n", ret);
    return ret;
  }

  if (ctx.ret != ZX_OK) {
    printf("mexec: error while enumerating e820 map. Retcode = %d\n", ctx.ret);
    return ctx.ret;
  }
  zbi::Zbi image(bootdata, len);
  zbi_result_t result;

  const uint32_t kNoZbiFlags = 0;
  const uint32_t kNoZbiExtra = 0;

  uint32_t section_length = (uint32_t)(sizeof(e820buf) - ctx.len);
  result =
      image.AppendSection(section_length, ZBI_TYPE_E820_TABLE, kNoZbiExtra, kNoZbiFlags, e820buf);

  if (result != ZBI_RESULT_OK) {
    printf(
        "mexec: Failed to append e820 map to zbi. len = %lu, section "
        "length = %u, retcode = %d\n",
        len, section_length, result);
    return ZX_ERR_INTERNAL;
  }

  // Append platform id
  if (bootloader.platform_id_size) {
    result = image.AppendSection(sizeof(bootloader.platform_id), ZBI_TYPE_PLATFORM_ID, kNoZbiExtra,
                                 kNoZbiFlags, reinterpret_cast<uint8_t*>(&bootloader.platform_id));
    if (result != ZBI_RESULT_OK) {
      printf(
          "mexec: Failed to append platform id to bootdata. "
          "len = %lu, section length = %lu, retcode = %d\n",
          len, sizeof(bootloader.platform_id), result);
      return ZX_ERR_INTERNAL;
    }
  }
  // Append information about the framebuffer to the bootdata
  if (bootloader.fb.base) {
    result = image.AppendSection(sizeof(bootloader.fb), ZBI_TYPE_FRAMEBUFFER, kNoZbiExtra,
                                 kNoZbiFlags, (uint8_t*)&bootloader.fb);
    if (result != ZBI_RESULT_OK) {
      printf(
          "mexec: Failed to append framebuffer data to bootdata. "
          "len = %lu, section length = %lu, retcode = %d\n",
          len, sizeof(bootloader.fb), result);
      return ZX_ERR_INTERNAL;
    }
  }

  if (bootloader.efi_system_table) {
    result = image.AppendSection(sizeof(bootloader.efi_system_table), ZBI_TYPE_EFI_SYSTEM_TABLE,
                                 kNoZbiExtra, kNoZbiFlags, (uint8_t*)&bootloader.efi_system_table);
    if (result != ZBI_RESULT_OK) {
      printf(
          "mexec: Failed to append efi sys table data to bootdata. "
          "len = %lu, section length = %lu, retcode = %d\n",
          len, sizeof(bootloader.efi_system_table), result);
      return ZX_ERR_INTERNAL;
    }
  }

  if (bootloader.acpi_rsdp) {
    result = image.AppendSection(sizeof(bootloader.acpi_rsdp), ZBI_TYPE_ACPI_RSDP, kNoZbiExtra,
                                 kNoZbiFlags, (uint8_t*)&bootloader.acpi_rsdp);
    if (result != ZBI_RESULT_OK) {
      printf(
          "mexec: Failed to append acpi rsdp data to bootdata. "
          "len = %lu, section length = %lu, retcode = %d\n",
          len, sizeof(bootloader.acpi_rsdp), result);
      return ZX_ERR_INTERNAL;
    }
  }

  if (bootloader.smbios) {
    result = image.AppendSection(sizeof(bootloader.smbios), ZBI_TYPE_SMBIOS, kNoZbiExtra,
                                 kNoZbiFlags, (uint8_t*)&bootloader.smbios);
    if (result != ZBI_RESULT_OK) {
      printf(
          "mexec: Failed to append smbios data to bootdata. len = %lu,"
          " section length = %lu, retcode = %d\n",
          len, sizeof(bootloader.smbios), result);
      return ZX_ERR_INTERNAL;
    }
  }

  if (bootloader.uart.type != ZBI_UART_NONE) {
    result = image.AppendSection(sizeof(bootloader.uart), ZBI_TYPE_DEBUG_UART, kNoZbiExtra,
                                 kNoZbiFlags, (uint8_t*)&bootloader.uart);
    if (result != ZBI_RESULT_OK) {
      printf(
          "mexec: Failed to append uart data to bootdata. len = %lu, "
          "section length = %lu, retcode = %d\n",
          len, sizeof(bootloader.uart), result);
      return ZX_ERR_INTERNAL;
    }
  }

  if (bootloader.nvram.base) {
    result = image.AppendSection(sizeof(bootloader.nvram), ZBI_TYPE_NVRAM, kNoZbiExtra, kNoZbiFlags,
                                 (uint8_t*)&bootloader.nvram);

    if (result != ZBI_RESULT_OK) {
      printf(
          "mexec: Failed to append nvram data to bootdata. len = %lu, "
          "section length = %lu, retcode = %d\n",
          len, sizeof(bootloader.nvram), result);
      return ZX_ERR_INTERNAL;
    }
  }

  return ZX_OK;
}

// Number of pages required to identity map 8GiB of memory.
constexpr size_t kBytesToIdentityMap = 8ull * GB;
constexpr size_t kNumL2PageTables = kBytesToIdentityMap / (2ull * MB * NO_OF_PT_ENTRIES);
constexpr size_t kNumL3PageTables = 1;
constexpr size_t kNumL4PageTables = 1;
constexpr size_t kTotalPageTableCount = kNumL2PageTables + kNumL3PageTables + kNumL4PageTables;

static fbl::RefPtr<VmAspace> mexec_identity_aspace;

// Array of pages that are safe to use for the new kernel's page tables.  These must
// be after where the new boot image will be placed during mexec.  This array is
// populated in platform_mexec_prep and used in platform_mexec.
static paddr_t mexec_safe_pages[kTotalPageTableCount];

void platform_mexec_prep(uintptr_t final_bootimage_addr, size_t final_bootimage_len) {
  DEBUG_ASSERT(!arch_ints_disabled());
  DEBUG_ASSERT(mp_get_online_mask() == cpu_num_to_mask(BOOT_CPU_ID));

  // A hacky way to handle disabling all PCI devices until we have devhost
  // lifecycles implemented.
  // Leaving PCI running will also leave DMA running which may cause memory
  // corruption after boot.
  // Disabling PCI may cause devices to fail to enumerate after boot.
#ifdef WITH_KERNEL_PCIE
  if (gCmdline.GetBool("kernel.mexec-pci-shutdown", true)) {
    PcieBusDriver::GetDriver()->DisableBus();
  }
#endif

  // This code only handles one L3 and one L4 page table for now. Fail if
  // there are more L2 page tables than can fit in one L3 page table.
  static_assert(kNumL2PageTables <= NO_OF_PT_ENTRIES,
                "Kexec identity map size is too large. Only one L3 PTE is supported at this time.");
  static_assert(kNumL3PageTables == 1, "Only 1 L3 page table is supported at this time.");
  static_assert(kNumL4PageTables == 1, "Only 1 L4 page table is supported at this time.");

  // Identity map the first 8GiB of RAM
  mexec_identity_aspace = VmAspace::Create(VmAspace::TYPE_LOW_KERNEL, "x86-64 mexec 1:1");
  DEBUG_ASSERT(mexec_identity_aspace);

  const uint perm_flags_rwx =
      ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE;
  void* identity_address = 0x0;
  paddr_t pa = 0;
  zx_status_t result =
      mexec_identity_aspace->AllocPhysical("1:1 mapping", kBytesToIdentityMap, &identity_address, 0,
                                           pa, VmAspace::VMM_FLAG_VALLOC_SPECIFIC, perm_flags_rwx);
  if (result != ZX_OK) {
    panic("failed to identity map low memory");
  }

  alloc_pages_greater_than(final_bootimage_addr + final_bootimage_len + PAGE_SIZE,
                           kTotalPageTableCount, kBytesToIdentityMap, mexec_safe_pages);
}

void platform_mexec(mexec_asm_func mexec_assembly, memmov_ops_t* ops, uintptr_t new_bootimage_addr,
                    size_t new_bootimage_len, uintptr_t entry64_addr) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(mp_get_online_mask() == cpu_num_to_mask(BOOT_CPU_ID));

  // This code only handles one L3 and one L4 page table for now. Fail if
  // there are more L2 page tables than can fit in one L3 page table.
  static_assert(kNumL2PageTables <= NO_OF_PT_ENTRIES,
                "Kexec identity map size is too large. Only one L3 PTE is supported at this time.");
  static_assert(kNumL3PageTables == 1, "Only 1 L3 page table is supported at this time.");
  static_assert(kNumL4PageTables == 1, "Only 1 L4 page table is supported at this time.");
  DEBUG_ASSERT(mexec_identity_aspace);

  vmm_set_active_aspace(reinterpret_cast<vmm_aspace_t*>(mexec_identity_aspace.get()));

  size_t safe_page_id = 0;
  volatile pt_entry_t* ptl4 = (pt_entry_t*)paddr_to_physmap(mexec_safe_pages[safe_page_id++]);
  volatile pt_entry_t* ptl3 = (pt_entry_t*)paddr_to_physmap(mexec_safe_pages[safe_page_id++]);

  // Initialize these to 0
  for (size_t i = 0; i < NO_OF_PT_ENTRIES; i++) {
    ptl4[i] = 0;
    ptl3[i] = 0;
  }

  for (size_t i = 0; i < kNumL2PageTables; i++) {
    ptl3[i] = mexec_safe_pages[safe_page_id] | X86_KERNEL_PD_FLAGS;
    volatile pt_entry_t* ptl2 = (pt_entry_t*)paddr_to_physmap(mexec_safe_pages[safe_page_id]);

    for (size_t j = 0; j < NO_OF_PT_ENTRIES; j++) {
      ptl2[j] = (2 * MB * (i * NO_OF_PT_ENTRIES + j)) | X86_KERNEL_PD_LP_FLAGS;
    }

    safe_page_id++;
  }

  ptl4[0] = vaddr_to_paddr((void*)ptl3) | X86_KERNEL_PD_FLAGS;

  mexec_assembly((uintptr_t)new_bootimage_addr, vaddr_to_paddr((void*)ptl4), entry64_addr, 0, ops,
                 0);
}

void platform_halt_secondary_cpus(void) {
  // Ensure the current thread is pinned to the boot CPU.
  DEBUG_ASSERT(get_current_thread()->hard_affinity == cpu_num_to_mask(BOOT_CPU_ID));

  // "Unplug" online secondary CPUs before halting them.
  cpu_mask_t primary = cpu_num_to_mask(BOOT_CPU_ID);
  cpu_mask_t mask = mp_get_online_mask() & ~primary;
  zx_status_t result = mp_unplug_cpu_mask(mask);
  DEBUG_ASSERT(result == ZX_OK);
}

void platform_early_init(void) {
  /* call before bootloader data is populated, since we want to
   * let the bootloader data override this */
  pc_init_debug_default_early();

  /* extract bootloader data while still accessible */
  /* this includes debug uart config, etc. */
  platform_save_bootloader_data();

  /* is the cmdline option to bypass dlog set ? */
  dlog_bypass_init();

  /* get the debug output working */
  pc_init_debug_early();

#if WITH_LEGACY_PC_CONSOLE
  /* get the text console working */
  platform_init_console();
#endif

  /* if the bootloader has framebuffer info, use it for early console */
  platform_early_display_init();

  /* initialize the boot memory reservation system */
  boot_reserve_init();

  /* add the ramdisk to the boot reserve list */
  platform_preserve_ramdisk();

  /* initialize physical memory arenas */
  pc_mem_init();

  /* wire all of the reserved boot sections */
  boot_reserve_wire();
}

static void platform_init_smp(void) {
  fbl::AllocChecker ac;
  fbl::Vector<uint32_t> apic_ids;

  // Filter out hyperthreads if we've been told not to init them
  const bool use_ht = gCmdline.GetBool("kernel.smp.ht", true);

  // We're implicitly running on the BSP
  const uint32_t bsp_apic_id = apic_local_id();
  DEBUG_ASSERT(bsp_apic_id == apic_bsp_id());

  // Iterate over all the cores, copy apic ids of active cores into list.
  dprintf(INFO, "cpu topology:\n");
  size_t cpu_index = 0;
  size_t bsp_apic_id_index = 0;
  for (const auto* processor_node : system_topology::GetSystemTopology().processors()) {
    const auto& processor = processor_node->entity.processor;
    for (size_t i = 0; i < processor.architecture_info.x86.apic_id_count; i++) {
      const uint32_t apic_id = processor.architecture_info.x86.apic_ids[i];
      const bool keep = (i < 1) || use_ht;

      dprintf(INFO, "\t%3zu: apic id %#4x %s%s%s\n", cpu_index++, apic_id, (i > 0) ? "SMT " : "",
              (apic_id == bsp_apic_id) ? "BSP " : "", keep ? "" : "(not using)");

      if (keep) {
        if (apic_id == bsp_apic_id) {
          bsp_apic_id_index = apic_ids.size();
        }

        apic_ids.push_back(apic_id, &ac);
        if (!ac.check()) {
          TRACEF("failed to allocate apic_ids table, disabling SMP\n");
          return;
        }
      }
    }
  }

  // Find the CPU count limit
  uint32_t max_cpus = gCmdline.GetUInt32("kernel.smp.maxcpus", SMP_MAX_CPUS);
  if (max_cpus > SMP_MAX_CPUS || max_cpus <= 0) {
    printf("invalid kernel.smp.maxcpus value, defaulting to %d\n", SMP_MAX_CPUS);
    max_cpus = SMP_MAX_CPUS;
  }

  dprintf(INFO, "Found %zu cpu%c\n", apic_ids.size(), (apic_ids.size() > 1) ? 's' : ' ');
  if (apic_ids.size() > max_cpus) {
    dprintf(INFO, "Clamping number of CPUs to %u\n", max_cpus);
    // TODO(edcoyne): Implement fbl::Vector()::resize().
    while (apic_ids.size() > max_cpus) {
      apic_ids.pop_back();
    }
  }

  if (apic_ids.size() == max_cpus || !use_ht) {
    // If we are at the max number of CPUs, or have filtered out
    // hyperthreads, sanity check that the bootstrap processor is in the set.
    bool found_bp = false;
    for (const auto apic_id : apic_ids) {
      if (apic_id == bsp_apic_id) {
        found_bp = true;
        break;
      }
    }
    ASSERT(found_bp);
  }

  x86_init_smp(apic_ids.data(), static_cast<uint32_t>(apic_ids.size()));

  // trim the boot cpu out of the apic id list before passing to the AP booting routine
  apic_ids.erase(bsp_apic_id_index);

  x86_bringup_aps(apic_ids.data(), static_cast<uint32_t>(apic_ids.size()));
}

zx_status_t platform_mp_prep_cpu_unplug(uint cpu_id) {
  // TODO: Make sure the IOAPIC and PCI have nothing for this CPU
  return arch_mp_prep_cpu_unplug(cpu_id);
}

const char* manufacturer = "unknown";
const char* product = "unknown";

void platform_init(void) {
  pc_init_debug();

  platform_init_crashlog();

#if NO_USER_KEYBOARD
  platform_init_keyboard(&console_input_buf);
#endif

  platform_init_smp();

  pc_init_smbios();

  SmbiosWalkStructs([](smbios::SpecVersion version, const smbios::Header* h,
                       const smbios::StringTable& st) -> zx_status_t {
    if (h->type == smbios::StructType::SystemInfo && version.IncludesVersion(2, 0)) {
      auto entry = reinterpret_cast<const smbios::SystemInformationStruct2_0*>(h);
      st.GetString(entry->manufacturer_str_idx, &manufacturer);
      st.GetString(entry->product_name_str_idx, &product);
    }
    return ZX_OK;
  });
  printf("smbios: manufacturer=\"%s\" product=\"%s\"\n", manufacturer, product);
}

void platform_suspend(void) {
  pc_prep_suspend_timer();
  pc_suspend_debug();
}

void platform_resume(void) {
  pc_resume_debug();
  pc_resume_timer();
}
