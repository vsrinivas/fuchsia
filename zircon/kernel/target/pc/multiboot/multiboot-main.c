// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <string.h>
#include <zircon/boot/e820.h>

#include <libzbi/zbi.h>

#include "trampoline.h"

#define BOOT_LOADER_NAME_ENV "multiboot.boot_loader_name="

static size_t zbi_size(const zbi_header_t* zbi) { return sizeof(*zbi) + zbi->length; }

// Convert the multiboot memory information to ZBI_TYPE_E820_TABLE format.
static void add_memory_info(void* zbi, size_t capacity, const multiboot_info_t* info) {
  if ((info->flags & MB_INFO_MMAP) && info->mmap_addr != 0 &&
      info->mmap_length >= sizeof(memory_map_t)) {
    size_t nranges = 0;
    for (memory_map_t* mmap = (void*)info->mmap_addr;
         (uintptr_t)mmap < info->mmap_addr + info->mmap_length;
         mmap = (void*)((uintptr_t)mmap + 4 + mmap->size)) {
      ++nranges;
    }
    e820entry_t* ranges;
    void* payload;
    zbi_result_t result = zbi_create_section(zbi, capacity, nranges * sizeof(ranges[0]),
                                             ZBI_TYPE_E820_TABLE, 0, 0, &payload);
    if (result != ZBI_RESULT_OK) {
      panic("zbi_create_section(%p, %#" PRIxPTR ", %#zx) failed: %d", zbi, capacity,
            nranges * sizeof(ranges[0]), (int)result);
    }
    ranges = payload;
    for (memory_map_t* mmap = (void*)info->mmap_addr;
         (uintptr_t)mmap < info->mmap_addr + info->mmap_length;
         mmap = (void*)((uintptr_t)mmap + 4 + mmap->size)) {
      *ranges++ = (e820entry_t){
          .addr = (((uint64_t)mmap->base_addr_high << 32) | mmap->base_addr_low),
          .size = (((uint64_t)mmap->length_high << 32) | mmap->length_low),
          // MB_MMAP_TYPE_* matches E820_* values.
          .type = mmap->type,
      };
    }
  } else {
    const e820entry_t ranges[] = {
        {
            .addr = 0,
            .size = info->mem_lower << 10,
            .type = E820_RAM,
        },
        {
            .addr = 1 << 20,
            .size = ((uint64_t)info->mem_upper << 10) - (1 << 20),
            .type = E820_RAM,
        },
    };
    zbi_result_t result =
        zbi_append_section(zbi, capacity, sizeof(ranges), ZBI_TYPE_E820_TABLE, 0, 0, ranges);
    if (result != ZBI_RESULT_OK) {
      panic("zbi_append_section(%p, %#" PRIxPTR ", %#zx) failed: %d", zbi, capacity, sizeof(ranges),
            (int)result);
    }
  }
}

static void add_cmdline(void* zbi, size_t capacity, const multiboot_info_t* info) {
  // Boot loader command line.
  if (info->flags & MB_INFO_CMD_LINE) {
    const char* cmdline = (void*)info->cmdline;
    size_t len = strlen(cmdline) + 1;
    zbi_result_t result = zbi_append_section(zbi, capacity, len, ZBI_TYPE_CMDLINE, 0, 0, cmdline);
    if (result != ZBI_RESULT_OK) {
      panic("zbi_append_section(%p, %#" PRIxPTR ", %zu) failed: %d", zbi, capacity, len,
            (int)result);
    }
  }

  // Boot loader name.
  if (info->flags & MB_INFO_BOOT_LOADER) {
    const char* name = (void*)info->boot_loader_name;
    size_t len = strlen(name) + 1;
    void* payload;
    zbi_result_t result = zbi_create_section(zbi, capacity, sizeof(BOOT_LOADER_NAME_ENV) - 1 + len,
                                             ZBI_TYPE_CMDLINE, 0, 0, &payload);
    if (result != ZBI_RESULT_OK) {
      panic("zbi_create_section(%p, %#" PRIxPTR ", %zu) failed: %d", zbi, capacity,
            sizeof(BOOT_LOADER_NAME_ENV) - 1 + len, (int)result);
    }
    for (char* p = (memcpy(payload, BOOT_LOADER_NAME_ENV, sizeof(BOOT_LOADER_NAME_ENV) - 1) +
                    sizeof(BOOT_LOADER_NAME_ENV) - 1);
         len > 0; ++p, ++name, --len) {
      *p = (*name == ' ' || *name == '\t' || *name == '\n' || *name == '\r') ? '+' : *name;
    }
  }
}

static void add_zbi_items(void* zbi, size_t capacity, const multiboot_info_t* info) {
  add_memory_info(zbi, capacity, info);
  add_cmdline(zbi, capacity, info);
}

static zbi_result_t find_kernel_item(zbi_header_t* hdr, void* payload, void* cookie) {
  if (hdr->type == ZBI_TYPE_KERNEL_X64) {
    *(const zbi_header_t**)cookie = hdr;
    return ZBI_RESULT_INCOMPLETE_KERNEL;
  }
  return ZBI_RESULT_OK;
}

noreturn void multiboot_main(uint32_t magic, multiboot_info_t* info) {
  if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
    panic("bad multiboot magic from bootloader %#" PRIx32 " != %#" PRIx32 "\n", magic,
          (uint32_t)MULTIBOOT_BOOTLOADER_MAGIC);
  }

  uintptr_t upper_memory_limit = 0;
  if (info->flags & MB_INFO_MEM_SIZE) {
    upper_memory_limit = info->mem_upper << 10;
    if (info->mem_upper > (UINT32_MAX >> 10)) {
      upper_memory_limit = -4096u;
    }
  } else if ((info->flags & MB_INFO_MMAP) && info->mmap_addr != 0 &&
             info->mmap_length >= sizeof(memory_map_t)) {
    for (memory_map_t* mmap = (void*)info->mmap_addr;
         (uintptr_t)mmap < info->mmap_addr + info->mmap_length;
         mmap = (void*)((uintptr_t)mmap + 4 + mmap->size)) {
      if (mmap->type == MB_MMAP_TYPE_AVAILABLE) {
        const uint64_t addr = (((uint64_t)mmap->base_addr_high << 32) | mmap->base_addr_low);
        const uint64_t len = (((uint64_t)mmap->length_high << 32) | mmap->length_low);
        const uint64_t end = addr + len;
        if (addr <= (uint64_t)(uintptr_t)PHYS_LOAD_ADDRESS &&
            end > (uint64_t)(uintptr_t)PHYS_LOAD_ADDRESS) {
          if (end > UINT32_MAX) {
            upper_memory_limit = -4096u;
          } else {
            upper_memory_limit = end;
          }
          break;
        }
      }
    }
    if (upper_memory_limit == 0) {
      panic("multiboot memory map doesn't cover %#" PRIxPTR, (uintptr_t)PHYS_LOAD_ADDRESS);
    }
  } else {
    panic("multiboot memory information missing");
  }

  if (!(info->flags & MB_INFO_MODS)) {
    panic("missing multiboot modules");
  }
  if (info->mods_count != 1) {
    panic("cannot handle multiboot mods_count %" PRIu32 " != 1\n", info->mods_count);
  }

  module_t* const mod = (void*)info->mods_addr;
  zbi_header_t* zbi = (void*)mod->mod_start;
  size_t zbi_len = mod->mod_end - mod->mod_start;

  if (zbi == NULL || zbi_len < sizeof(*zbi)) {
    panic("insufficient multiboot module [%#" PRIx32 ",%#" PRIx32
          ")"
          " for ZBI header",
          mod->mod_start, mod->mod_end);
  }

  // TODO(crbug.com/917455): Sanity check disabled for now because Depthcharge
  // as of
  // https://chromium.googlesource.com/chromiumos/platform/depthcharge/+/firmware-eve-9584.B
  // prepends items and adjusts the ZBI container header, but fails to update
  // the Multiboot module_t header to match.  This is now fixed upstream by
  // https://chromium.googlesource.com/chromiumos/platform/depthcharge/+/b80fb0a9b04c97769ffe73babddf0aa9e3bc0b94#
  // but not yet rolled out to all devices.
  if (zbi_len < sizeof(*zbi) + zbi->length && 0) {
    panic("insufficient multiboot module [%#" PRIx32 ",%#" PRIx32
          ")"
          " for ZBI length %#" PRIx32,
          mod->mod_start, mod->mod_end, sizeof(*zbi) + zbi->length);
  }

  // Depthcharge prepends items to the ZBI, so the kernel is not necessarily
  // first in the image seen here even though that is a requirement of the
  // protocol with actual ZBI bootloaders.  Hence this can't use
  // zbi_check_complete.
  zbi_header_t* bad_hdr;
  zbi_result_t result = zbi_check(zbi, &bad_hdr);
  if (result != ZBI_RESULT_OK) {
    panic("ZBI failed check: %d at offset %#zx", (int)result,
          (size_t)((uint8_t*)bad_hdr - (uint8_t*)zbi));
  }

  // Find the kernel item.
  const zbi_header_t* kernel_item_header = NULL;
  result = zbi_for_each(zbi, &find_kernel_item, &kernel_item_header);
  if (result != ZBI_RESULT_INCOMPLETE_KERNEL) {
    panic("ZBI missing kernel");
  }

  // This is the kernel item's payload, but it expects the whole
  // zircon_kernel_t (i.e. starting with the container header) to be loaded
  // at PHYS_LOAD_ADDRESS.
  const zbi_kernel_t* kernel_header = (const void*)(kernel_item_header + 1);

  // The kernel will sit at PHYS_LOAD_ADDRESS, where the code now
  // running sits.  The space until kernel_memory_end is reserved
  // and can't be used for anything else.
  const size_t kernel_load_size =
      offsetof(zircon_kernel_t, data_kernel) + kernel_item_header->length;
  uint8_t* const kernel_load_end = PHYS_LOAD_ADDRESS + kernel_load_size;
  uint8_t* const kernel_memory_end =
      kernel_load_end + ZBI_ALIGN(kernel_header->reserve_memory_size);

  if (upper_memory_limit < (uintptr_t)kernel_memory_end) {
    panic("upper memory limit %#" PRIxPTR " < kernel end %p", upper_memory_limit,
          kernel_memory_end);
  }

  // Now we can append other items to the ZBI.
  const size_t capacity = upper_memory_limit - (uintptr_t)zbi;
  add_zbi_items(zbi, capacity, info);

  // Use discarded ZBI space to hold the trampoline.
  void* trampoline;
  result = zbi_create_section(zbi, capacity, sizeof(struct trampoline), ZBI_TYPE_DISCARD, 0, 0,
                              &trampoline);
  if (result != ZBI_RESULT_OK) {
    panic("zbi_create_section(%p, %#" PRIxPTR ", %#zx) failed: %d", zbi, capacity,
          sizeof(struct trampoline), (int)result);
  }

  uint8_t* const zbi_end = (uint8_t*)zbi + zbi_size(zbi);
  uintptr_t free_memory = (uintptr_t)kernel_memory_end;
  if ((uint8_t*)zbi < kernel_memory_end) {
    // The ZBI overlaps where the kernel needs to sit.  Copy it further up.
    zbi_header_t* new_zbi = (void*)zbi_end;
    if ((uint8_t*)zbi_end < kernel_memory_end) {
      new_zbi = (void*)kernel_memory_end;
    }
    // It needs to be page-aligned.
    new_zbi = (void*)(((uintptr_t)new_zbi + 4096 - 1) & -4096);
    memmove(new_zbi, zbi, zbi_size(zbi));
    free_memory = (uintptr_t)new_zbi + zbi_size(zbi);
    zbi = new_zbi;
  }

  // Set up page tables in free memory.
  enable_64bit_paging(free_memory, upper_memory_limit);

  // Copy the kernel into place and enter its code in 64-bit mode.
  boot_zbi(zbi, kernel_item_header, trampoline);
}
