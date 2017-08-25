// Copyright 2017 The Fuchsia Authors
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdlib.h>
#include <arch/ops.h>
#include <arch/efi.h>
#include <kernel/vm.h>
#include <inttypes.h>
#include <string.h>

efi_system_table_t *sys_table = NULL;

uint64_t efi_boot(void* handle, efi_system_table_t *systable, paddr_t image_addr) __EXTERNALLY_VISIBLE;

static uint32_t efi_utf16_ascii_len(const uint16_t *src, int n) {
    uint32_t count = 0;
    uint16_t c;
    while (n--) {
        c = *src++;
        if (c < 0x80)
            count++;
    }
    return count;
}

static char *efi_utf16_to_ascii(char *dst, const uint16_t *src, int n)
{
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

static void efi_print(const char *str)
{
    int i;
    struct efi_simple_text_output_protocol *out;
    if (sys_table) {
        out = (struct efi_simple_text_output_protocol *)sys_table->con_out;

        for (i = 0; str[i]; i++) {
            efi_char16_t ch[2] = { 0 };

            ch[0] = str[i];
            if (str[i] == '\n') {
                efi_char16_t nl[2] = { '\r', 0 };
                out->output_string(out, nl);
            }
            out->output_string(out, ch);
        }
    }
}
#if 1
#define efi_printf(args...)                 \
    do {                                    \
        char buff[256];                     \
        snprintf(buff,sizeof(buff),args);   \
        efi_print(buff);                    \
    } while(0);
#else
#define efi_printf(args...)
#endif

extern uint64_t _start;
extern uint64_t _end;

uint64_t efi_boot(void* handle, efi_system_table_t *systable, paddr_t image_addr) {

    efi_status_t status;
    efi_loaded_image_t *image;
    efi_guid_t loaded_image_proto = LOADED_IMAGE_PROTOCOL_GUID;

    sys_table = systable;

    efi_printf("Booting Magenta from EFI loader...\n");

    status = systable->boottime->handle_protocol(handle,
                    &loaded_image_proto, (void **)&image);
    if (status != EFI_SUCCESS) {
        efi_printf("Failed to get loaded image protocol\n");
        return 0;
    }

    // Allocate space for new kernel location (+bss)
    uint64_t kern_pages = (uint64_t)&_end - (uint64_t)&_start;
    kern_pages = ROUNDUP(kern_pages, EFI_ALLOC_ALIGN) / EFI_PAGE_SIZE;
    efi_physical_addr_t target_addr = MEMBASE + KERNEL_LOAD_OFFSET;
    status = systable->boottime->allocate_pages( EFI_ALLOCATE_ADDRESS,
                                                 EFI_LOADER_DATA,
                                                 kern_pages,
                                                 &target_addr);
    if (status != EFI_SUCCESS) {
        efi_printf("Failed to allocate space for kernel\n");
        return 0;
    }

    // Copy kernel to new location
    memcpy((void*)target_addr,(void*)image_addr,kern_pages*EFI_PAGE_SIZE);


    efi_magenta_hdr_t *mag_hdr;

    uint32_t cmd_line_len = efi_utf16_ascii_len((const uint16_t*)image->load_options,image->load_options_size/2) + 1;

    status = systable->boottime->allocate_pool(EFI_LOADER_DATA, sizeof(*mag_hdr) + cmd_line_len,
                                                                (void **)&mag_hdr);
    if (status != EFI_SUCCESS) {
        efi_printf("Failed to allocate space for magenta boot args\n");
        return 0;
    }

    efi_printf("Magenta boot args address= %p\n",(void*)mag_hdr);

    mag_hdr->magic = EFI_MAGENTA_MAGIC;
    mag_hdr->cmd_line_len = cmd_line_len;
    efi_utf16_to_ascii(mag_hdr->cmd_line, (const uint16_t*)image->load_options, image->load_options_size/2);
    mag_hdr->cmd_line[cmd_line_len-1]=0;

    efi_printf("Magenta cmdline args = %s\n",mag_hdr->cmd_line);
    const char token[] = "initrd=";
    char* pos;
    uint64_t initrd_start_phys=0;
    uint64_t initrd_size=0;
    pos = strstr(mag_hdr->cmd_line,token);
    if (pos) {
        pos = pos + strlen(token);
        initrd_start_phys = strtoll(pos,&pos,16);
        pos++;
        initrd_size = strtoll(pos,&pos,16);
    }

    if (initrd_start_phys && initrd_size) {
        uint64_t ramdisk_pages = ROUNDUP_PAGE_SIZE(initrd_size) / PAGE_SIZE;
        /* TODO - figure out how to pull this from boot image header */
        efi_physical_addr_t ramdisk_target_addr = 0x07c00000;

        status = systable->boottime->allocate_pages( EFI_ALLOCATE_ADDRESS,
                                                     EFI_LOADER_DATA,
                                                     ramdisk_pages,
                                                     &ramdisk_target_addr);
        if (status != EFI_SUCCESS) {
            efi_printf("Failed to allocate space for ramdisk\n");
            return 0;
        }
        mag_hdr->ramdisk_base_phys = (uint64_t)ramdisk_target_addr;
        mag_hdr->ramdisk_size = (uint64_t)ROUNDUP_PAGE_SIZE(initrd_size);

        // Copy kernel to new location
        memcpy((void*)ramdisk_target_addr,
               (void*)initrd_start_phys,initrd_size);

        arch_sync_cache_range((addr_t)ramdisk_target_addr,initrd_size);
        efi_printf("initrd found and flushed from cache...\n");
    } else {
        efi_printf("initrd not found!!!!!\n");
        return 0;
    }

    // sync cache (we jumped here with mmu on w/ identity and cache on)
    arch_sync_cache_range((addr_t)target_addr, kern_pages*EFI_PAGE_SIZE);
    arch_sync_cache_range((addr_t)mag_hdr, sizeof(*mag_hdr) + cmd_line_len);

    return (uint64_t)mag_hdr;
}