// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "osboot.h"

#include <efi/protocol/graphics-output.h>
#include <efi/runtime-services.h>
#include <efi/zircon.h>

#include <cmdline.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <xefi.h>

#include <zircon/boot/image.h>
#include <zircon/pixelformat.h>


static efi_guid zircon_guid = ZIRCON_VENDOR_GUID;
static char16_t crashlog_name[] = ZIRCON_CRASHLOG_EFIVAR;

static size_t get_last_crashlog(efi_system_table* sys, void* ptr, size_t max) {
    efi_runtime_services* rs = sys->RuntimeServices;

    uint32_t attr = ZIRCON_CRASHLOG_EFIATTR;
    size_t sz = max;
    efi_status r = rs->GetVariable(crashlog_name, &zircon_guid, &attr, &sz, ptr);
    if (r == EFI_SUCCESS) {
        // Erase it
        rs->SetVariable(crashlog_name, &zircon_guid, ZIRCON_CRASHLOG_EFIATTR, 0, NULL);
    } else {
        sz = 0;
    }
    return sz;
}

static unsigned char scratch[32768];

static void start_zircon(uint64_t entry, void* bootdata) {
#if __x86_64__
    // ebx = 0, ebp = 0, edi = 0, esi = bootdata
    __asm__ __volatile__(
        "movl $0, %%ebp \n"
        "cli \n"
        "jmp *%[entry] \n" ::[entry] "a"(entry),
        [bootdata] "S"(bootdata),
        "b"(0), "D"(0));
#else
#warning "add code for other arches here"
#endif
    for (;;)
        ;
}

static int add_bootdata(void** ptr, size_t* avail,
                        zbi_header_t* bd, void* data) {
    size_t len = ZBI_ALIGN(bd->length);
    if ((sizeof(zbi_header_t) + len) > *avail) {
        printf("boot: no room for bootdata type=%08x size=%08x\n",
               bd->type, bd->length);
        return -1;
    }
    bd->flags |= ZBI_FLAG_VERSION;
    bd->reserved0 = 0;
    bd->reserved1 = 0;
    bd->magic = ZBI_ITEM_MAGIC;
    bd->crc32 = ZBI_ITEM_NO_CRC32;

    memcpy(*ptr, bd, sizeof(zbi_header_t));
    memcpy((*ptr) + sizeof(zbi_header_t), data, len);
    len += sizeof(zbi_header_t);
    (*ptr) += len;
    (*avail) -= len;

    return 0;
}

size_t image_getsize(void* image, size_t sz) {
    if (sz < sizeof(zircon_kernel_t)) {
        return 0;
    }
    zircon_kernel_t* kernel = image;
    if ((kernel->hdr_file.type != ZBI_TYPE_CONTAINER) ||
        (kernel->hdr_file.magic != ZBI_ITEM_MAGIC) ||
        (kernel->hdr_kernel.type != ZBI_TYPE_KERNEL_X64) ||
        (kernel->hdr_kernel.magic != ZBI_ITEM_MAGIC)) {
        return 0;
    }
    return ZBI_ALIGN(kernel->hdr_file.length) + sizeof(zbi_header_t);
}

static int header_check(void* image, size_t sz, uint64_t* _entry,
                        size_t* _flen, size_t* _klen) {
    zbi_header_t* bd = image;
    size_t flen, klen;
    uint64_t entry;

    if (!(bd->flags & ZBI_FLAG_VERSION)) {
        printf("boot: v1 bootdata kernel no longer supported\n");
        return -1;
    }
    zircon_kernel_t* kernel = image;
    if ((sz < sizeof(zircon_kernel_t)) ||
        (kernel->hdr_kernel.type != ZBI_TYPE_KERNEL_X64) ||
        ((kernel->hdr_kernel.flags & ZBI_FLAG_VERSION) == 0)) {
        printf("boot: invalid zircon kernel header\n");
        return -1;
    }
    flen = ZBI_ALIGN(kernel->hdr_file.length);
    klen = ZBI_ALIGN(kernel->hdr_kernel.length);
    entry = kernel->data_kernel.entry;
    if (flen > (sz - sizeof(zbi_header_t))) {
        printf("boot: invalid zircon kernel header (bad flen)\n");
        return -1;
    }

    if (klen > (sz - (sizeof(zbi_header_t) * 2))) {
        printf("boot: invalid zircon kernel header (bad klen)\n");
        return -1;
    }
    if (_entry) {
        *_entry = entry;
    }
    if (_flen) {
        *_flen = flen;
        *_klen = klen;
    }

    return 0;
}

static int item_check(zbi_header_t* bd, size_t sz) {
    if (sz > 0x7FFFFFFF) {
        // disallow 2GB+ items to avoid wrap on align issues
        return -1;
    }
    if ((bd->magic != ZBI_ITEM_MAGIC) ||
        ((bd->flags & ZBI_FLAG_VERSION) == 0) ||
        (ZBI_ALIGN(bd->length) > sz)) {
        return -1;
    } else {
        return 0;
    }
}

//TODO: verify crc32 when present
unsigned identify_image(void* image, size_t sz) {
    if (sz == 0) {
        return IMAGE_EMPTY;
    }
    if (sz < sizeof(zbi_header_t)) {
        printf("image is too small\n");
        return IMAGE_INVALID;
    }
    zbi_header_t* bd = image;
    sz -= sizeof(zbi_header_t);
    if ((bd->type != ZBI_TYPE_CONTAINER) ||
        item_check(bd, sz)) {
        printf("image has invalid header\n");
        return IMAGE_INVALID;
    }
    image += sizeof(zbi_header_t);
    unsigned n = 0;
    unsigned r = 0;
    while (sz > sizeof(zbi_header_t)) {
        bd = image;
        sz -= sizeof(zbi_header_t);
        if (item_check(image, sz)) {
            printf("image has invalid bootitem\n");
            return IMAGE_INVALID;
        }
        if (ZBI_IS_KERNEL_BOOTITEM(bd->type)) {
            if (n != 0) {
                printf("image has kernel in middle\n");
                return IMAGE_INVALID;
            } else {
                r = IMAGE_KERNEL;
            }
        }
        if (bd->type == ZBI_TYPE_STORAGE_BOOTFS) {
            if ((r == IMAGE_KERNEL) || (r == IMAGE_COMBO)) {
                r = IMAGE_COMBO;
            } else {
                r = IMAGE_RAMDISK;
            }
        }
        image += ZBI_ALIGN(bd->length) + sizeof(zbi_header_t);
        sz -= ZBI_ALIGN(bd->length);
        n++;
    }

    return r;
}

int boot_zircon(efi_handle img, efi_system_table* sys,
                 void* image, size_t isz, void* ramdisk, size_t rsz,
                 void* cmdline, size_t csz) {

    efi_boot_services* bs = sys->BootServices;
    uint64_t entry;

    if (header_check(image, isz, &entry, NULL, NULL)) {
        return -1;
    }
    if ((ramdisk == NULL) || (rsz < sizeof(zbi_header_t))) {
        printf("boot: ramdisk missing or too small\n");
        return -1;
    }
    if (isz > kernel_zone_size) {
        printf("boot: kernel image too large\n");
        return -1;
    }

    zbi_header_t* hdr0 = ramdisk;
    if ((hdr0->type != ZBI_TYPE_CONTAINER) ||
        (hdr0->extra != ZBI_CONTAINER_MAGIC) ||
        !(hdr0->flags & ZBI_FLAG_VERSION)) {
        printf("boot: ramdisk has invalid bootdata header\n");
        return -1;
    }

    if ((hdr0->length > (rsz - sizeof(zbi_header_t)))) {
        printf("boot: ramdisk has invalid bootdata length\n");
        return -1;
    }

    // osboot ensures we have FRONT_BYTES ahead of the
    // ramdisk to prepend our own bootdata items.
    void* bptr = ramdisk - FRONT_BYTES;
    size_t blen = FRONT_BYTES;

    // We create a new container header of the same size
    // as the one at the start of the ramdisk
    zbi_header_t hdr = ZBI_CONTAINER_HEADER(hdr0->length + FRONT_BYTES);
    memcpy(bptr, &hdr, sizeof(hdr));
    bptr += sizeof(hdr);

    // pass kernel commandline
    hdr.type = ZBI_TYPE_CMDLINE;
    hdr.length = csz;
    hdr.extra = 0;
    hdr.flags = ZBI_FLAG_VERSION;
    if (add_bootdata(&bptr, &blen, &hdr, cmdline)) {
        return -1;
    }

    // pass ACPI root pointer
    uint64_t rsdp = find_acpi_root(img, sys);
    if (rsdp != 0) {
        hdr.type = ZBI_TYPE_ACPI_RSDP;
        hdr.length = sizeof(rsdp);
        if (add_bootdata(&bptr, &blen, &hdr, &rsdp)) {
            return -1;
        }
    }

    // pass SMBIOS entry point pointer
    uint64_t smbios = find_smbios(img, sys);
    if (smbios != 0) {
        hdr.type = ZBI_TYPE_SMBIOS;
        hdr.length = sizeof(smbios);
        if (add_bootdata(&bptr, &blen, &hdr, &smbios)) {
            return -1;
        }
    }

    // pass EFI system table
    uint64_t addr = (uintptr_t) sys;
    hdr.type = ZBI_TYPE_EFI_SYSTEM_TABLE;
    hdr.length = sizeof(sys);
    if (add_bootdata(&bptr, &blen, &hdr, &addr)) {
        return -1;
    }

    // pass framebuffer data
    efi_graphics_output_protocol* gop = NULL;
    bs->LocateProtocol(&GraphicsOutputProtocol, NULL, (void**)&gop);
    if (gop) {
        zbi_swfb_t fb = {
            .base = gop->Mode->FrameBufferBase,
            .width = gop->Mode->Info->HorizontalResolution,
            .height = gop->Mode->Info->VerticalResolution,
            .stride = gop->Mode->Info->PixelsPerScanLine,
            .format = get_zx_pixel_format(gop),
        };
        hdr.type = ZBI_TYPE_FRAMEBUFFER;
        hdr.length = sizeof(fb);
        if (add_bootdata(&bptr, &blen, &hdr, &fb)) {
            return -1;
        }
    }

    memcpy((void*)kernel_zone_base, image, isz);

    // Obtain the system memory map
    size_t msize, dsize;
    for (int attempts = 0;;attempts++) {
        efi_memory_descriptor* mmap = (efi_memory_descriptor*) (scratch + sizeof(uint64_t));
        uint32_t dversion = 0;
        size_t mkey = 0;
        msize = sizeof(scratch) - sizeof(uint64_t);
        dsize = 0;
        efi_status r = sys->BootServices->GetMemoryMap(&msize, mmap, &mkey, &dsize, &dversion);
        if (r != EFI_SUCCESS) {
            printf("boot: cannot GetMemoryMap()\n");
            goto fail;
        }

        r = sys->BootServices->ExitBootServices(img, mkey);
        if (r == EFI_SUCCESS) {
            break;
        }
        if (r == EFI_INVALID_PARAMETER) {
            if (attempts > 0) {
                printf("boot: cannot ExitBootServices(): %s\n", xefi_strerror(r));
                goto fail;
            }
            // Attempting to exit may cause us to have to re-grab the
            // memory map, but if it happens more than once something's
            // broken.
            continue;
        }
        printf("boot: cannot ExitBootServices(): %s\n", xefi_strerror(r));
        goto fail;
    }
    memcpy(scratch, &dsize, sizeof(uint64_t));

    // install memory map
    hdr.type = ZBI_TYPE_EFI_MEMORY_MAP;
    hdr.length = msize + sizeof(uint64_t);
    if (add_bootdata(&bptr, &blen, &hdr, scratch)) {
        goto fail;
    }

    // obtain the last crashlog if we can
    size_t sz = get_last_crashlog(sys, scratch, 4096);
    if (sz > 0) {
        hdr.type = ZBI_TYPE_CRASHLOG;
        hdr.length = sz;
        add_bootdata(&bptr, &blen, &hdr, scratch);
    }

    // fill the remaining gap between pre-data and ramdisk image
    if ((blen < sizeof(hdr)) || (blen & 7)) {
        goto fail;
    }
    hdr.type = ZBI_TYPE_DISCARD;
    hdr.length = blen - sizeof(hdr);
    hdr.flags = ZBI_FLAG_VERSION;
    memcpy(bptr, &hdr, sizeof(hdr));

    // jump to the kernel
    start_zircon(entry, ramdisk - FRONT_BYTES);

fail:
    return -1;
}

static char cmdline[CMDLINE_MAX];

int zedboot(efi_handle img, efi_system_table* sys,
            void* image, size_t sz) {

    size_t flen, klen;
    if (header_check(image, sz, NULL, &flen, &klen)) {
        return -1;
    }

    // ramdisk portion is file - headers - kernel len
    uint32_t rlen = flen - sizeof(zbi_header_t) - klen;
    uint32_t roff = (sizeof(zbi_header_t) * 2) + klen;
    if (rlen == 0) {
        printf("zedboot: no ramdisk?!\n");
        return -1;
    }

    // allocate space for the ramdisk
    efi_boot_services* bs = sys->BootServices;
    size_t rsz = rlen + sizeof(zbi_header_t) + FRONT_BYTES;
    size_t pages = BYTES_TO_PAGES(rsz);
    void* ramdisk = NULL;
    efi_status r = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, pages,
                                     (efi_physical_addr*)&ramdisk);
    if (r) {
        printf("zedboot: cannot allocate ramdisk buffer\n");
        return -1;
    }

    ramdisk += FRONT_BYTES;
    *(zbi_header_t*)ramdisk = (zbi_header_t)ZBI_CONTAINER_HEADER(rlen);
    memcpy(ramdisk + sizeof(zbi_header_t), image + roff, rlen);
    rlen += sizeof(zbi_header_t);

    printf("ramdisk @ %p\n", ramdisk);

    size_t csz = cmdline_to_string(cmdline, sizeof(cmdline));

    // shrink original image header to include only the kernel
    zircon_kernel_t* kernel = image;
    kernel->hdr_file.length = sizeof(zbi_header_t) + klen;

    return boot_zircon(img, sys, image, roff, ramdisk, rlen, cmdline, csz);
}

int boot_kernel(efi_handle img, efi_system_table* sys,
                void* image, size_t sz,
                void* ramdisk, size_t rsz) {

    size_t csz = cmdline_to_string(cmdline, sizeof(cmdline));

    zbi_header_t* bd = image;
    if ((bd->type == ZBI_TYPE_CONTAINER) &&
        (bd->extra == ZBI_CONTAINER_MAGIC)) {
        return boot_zircon(img, sys, image, sz, ramdisk, rsz, cmdline, csz);
    } else {
        return -1;
    }
}
