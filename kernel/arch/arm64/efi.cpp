// Copyright 2017 The Fuchsia Authors
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/efi.h>

#include <arch/ops.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <vm/vm.h>

static efi_system_table_t* sys_table = nullptr;

static uint32_t efi_utf16_ascii_len(const uint16_t* src, int n) {
    uint32_t count = 0;
    uint16_t c;
    while (n--) {
        c = *src++;
        if (c < 0x80)
            count++;
    }
    return count;
}

static char* efi_utf16_to_ascii(char* dst, const uint16_t* src, int n) {
    uint32_t c;

    while (n--) {
        c = *src++;
        if (c < 0x80) {
            *dst++ = (char)c;
            continue;
        }
        if (c < 0x800) {
            *dst++ = (char)(0xc0 + (c >> 6));
            goto t1;
        }
        if (c < 0x10000) {
            *dst++ = (char)(0xe0 + (c >> 12));
            goto t2;
        }
        *dst++ = (char)(0xf0 + (c >> 18));
        *dst++ = (char)(0x80 + ((c >> 12) & 0x3f));
    t2:
        *dst++ = (char)(0x80 + ((c >> 6) & 0x3f));
    t1:
        *dst++ = (char)(0x80 + (c & 0x3f));
    }

    return dst;
}

static void efi_print(const char* str) {
    int i;
    struct efi_simple_text_output_protocol* out;
    if (sys_table) {
        out = (struct efi_simple_text_output_protocol*)sys_table->con_out;

        for (i = 0; str[i]; i++) {
            efi_char16_t ch[2] = {0};

            ch[0] = str[i];
            if (str[i] == '\n') {
                efi_char16_t nl[2] = {'\r', 0};
                out->output_string(out, nl);
            }
            out->output_string(out, ch);
        }
    }
}

static int efi_printf(const char* fmt, ...) __PRINTFLIKE(1, 2);
static int efi_printf(const char* fmt, ...) {
    va_list ap;

    char buf[256];

    va_start(ap, fmt);
    int err = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    efi_print(buf);

    return err;
}

static void efi_abort() __NO_RETURN;
static void efi_abort() {
    efi_printf("EFI: aborting, spinning forever\n");
    for (;;)
        arch_idle();
}

// align the kernel allocations to a mid sized page so it might be able to use it
#define KERNEL_ALIGN (64 * 1024)

// make sure there's a largish gap after the kernel for boot time allocations
#define KERNEL_TAIL_PADDING (16 * 1024 * 1024)

efi_boot_ret efi_boot(void* handle, efi_system_table_t* systable, uint64_t image_addr) {

    efi_status_t status;

    sys_table = systable;

    efi_printf("EFI: booting Zircon from EFI loader...\n");
    efi_printf("EFI: currently running at address %#" PRIxPTR " EL%lu\n",
            image_addr, ARM64_READ_SYSREG(currentel) >> 2);

    efi_loaded_image_t* image;
    efi_guid_t loaded_image_proto = LOADED_IMAGE_PROTOCOL_GUID;
    status = systable->boottime->handle_protocol(handle,
                                                 &loaded_image_proto, (void**)&image);
    if (status != EFI_SUCCESS) {
        efi_printf("EFI: failed to get loaded image protocol\n");
        efi_abort();
    }

    uint32_t cmd_line_len = efi_utf16_ascii_len((const uint16_t*)image->load_options, image->load_options_size / 2) + 1;

    // allocate space for the header passed to the kernel
    efi_zircon_hdr_t* mag_hdr = {};
    status = systable->boottime->allocate_pool(EFI_LOADER_DATA, sizeof(*mag_hdr) + cmd_line_len,
                                               (void**)&mag_hdr);
    if (status != EFI_SUCCESS) {
        efi_printf("EFI: failed to allocate space for zircon boot args\n");
        efi_abort();
    }

    efi_printf("EFI: Zircon boot args address %p\n", (void*)mag_hdr);

    *mag_hdr = {};
    mag_hdr->magic = EFI_ZIRCON_MAGIC;
    mag_hdr->cmd_line_len = cmd_line_len;
    efi_utf16_to_ascii(mag_hdr->cmd_line, (const uint16_t*)image->load_options, image->load_options_size / 2);
    mag_hdr->cmd_line[cmd_line_len - 1] = 0;

    efi_printf("EFI: Zircon cmdline args = '%s'\n", mag_hdr->cmd_line);
    const char token[] = "initrd=";
    char* pos;
    uint64_t initrd_start_phys = 0;
    uint64_t initrd_size = 0;
    pos = strstr(mag_hdr->cmd_line, token);
    if (pos) {
        pos = pos + strlen(token);
        initrd_start_phys = strtoll(pos, &pos, 16);
        pos++;
        initrd_size = strtoll(pos, &pos, 16);
    }

    if (!(initrd_start_phys && initrd_size)) {
        efi_printf("EFI: initrd not found!!!!!\n");
        efi_abort();
    }
    efi_printf("EFI: initrd found: base %#" PRIx64 ", length %#" PRIx64 "\n", initrd_start_phys, initrd_size);

    // we're going to allocate a large chunk for the ramdisk + kernel, compute the size
    uint64_t alloc_pages = 0;

    // compute the size for the kernel
    uint64_t kern_pages = get_kernel_size();
    kern_pages = ROUNDUP(kern_pages, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
    alloc_pages += kern_pages;

    uint64_t ramdisk_pages = ROUNDUP_PAGE_SIZE(initrd_size) / PAGE_SIZE;
    alloc_pages += ramdisk_pages;

    // allocate a large chunk for both the ramdisk and kernel, back to back
    uint64_t alloc_addr = 0;
    status = systable->boottime->allocate_pages(EFI_ALLOCATE_ANY_PAGES,
                                                EFI_LOADER_DATA,
                                                alloc_pages + (KERNEL_ALIGN + KERNEL_TAIL_PADDING) / EFI_PAGE_SIZE,
                                                &alloc_addr);
    if (status != EFI_SUCCESS) {
        efi_printf("EFI: failed to allocate space for ramdisk and kernel\n");
        efi_abort();
    }
    efi_printf("EFI: big allocation base at %#" PRIx64 "\n", alloc_addr);

    // ramdisk is at the base of this new allocation, page alignment is fine
    uint64_t ramdisk_target_addr = alloc_addr;
    efi_printf("EFI: new ramdisk address %#" PRIxPTR "\n", ramdisk_target_addr);

    mag_hdr->ramdisk_base_phys = (uint64_t)ramdisk_target_addr;
    mag_hdr->ramdisk_size = (uint64_t)ROUNDUP_PAGE_SIZE(initrd_size);

    // Copy ramdisk to new location
    memcpy((void*)ramdisk_target_addr,
           (void*)initrd_start_phys, initrd_size);


    // kernel will be located at the next aligned boundary after the ramdisk
    efi_physical_addr_t kernel_target_addr = ramdisk_target_addr + initrd_size;
    kernel_target_addr = ROUNDUP(kernel_target_addr, KERNEL_ALIGN);
    efi_printf("EFI: new kernel address (rounded up) %#" PRIxPTR "\n", kernel_target_addr);

    // Copy kernel to new location
    memcpy((void*)kernel_target_addr, (void*)image_addr, get_kernel_size());

    // make sure everything is fully written out to memory
    efi_printf("EFI: cleaning data cache\n");
    arch_clean_cache_range((addr_t)ramdisk_target_addr, initrd_size);
    arch_clean_cache_range((addr_t)kernel_target_addr, kern_pages * EFI_PAGE_SIZE);
    arch_clean_cache_range((addr_t)mag_hdr, sizeof(*mag_hdr) + cmd_line_len);

    // get the current memory map
    uint8_t map[4096] = {};
    uint64_t memory_map_size = sizeof(map);
    uint64_t map_key;
    uint64_t desc_size;
    uint32_t desc_ver;
    status = systable->boottime->get_memory_map(
        &memory_map_size,
        &map,
        &map_key,
        &desc_size,
        &desc_ver);
    if (status != EFI_SUCCESS) {
        efi_printf("failed to get memory map\n");
        efi_abort();
    }

    efi_printf("EFI: map size %lu desc size %lu ver %u\n", memory_map_size, desc_size, desc_ver);

    for (size_t i = 0; i < memory_map_size / desc_size; i++) {
        efi_memory_desc_t* desc = (efi_memory_desc_t*)(map + i * desc_size);
        efi_printf("%4zu: type %u phys %#lx num_pages %lu attr %#lx\n",
                   i, desc->type, desc->phys_addr, desc->num_pages, desc->attribute);
    }

    // exit boot services
    efi_printf("EFI: exiting boot services and branching into new kernel\n");
    status = systable->boottime->exit_boot_services(handle, map_key);
    if (status != EFI_SUCCESS) {
        efi_printf("EFI: failed to exit boot services\n");
        efi_abort();
    }

    return {mag_hdr, kernel_target_addr};
}
