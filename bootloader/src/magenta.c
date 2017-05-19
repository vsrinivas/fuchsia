// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "osboot.h"

#include <efi/protocol/graphics-output.h>
#include <efi/runtime-services.h>

#include <cmdline.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <xefi.h>

#include <magenta/boot/bootdata.h>
#include <magenta/pixelformat.h>


static efi_guid magenta_guid = MAGENTA_VENDOR_GUID;
static char16_t crashlog_name[] = MAGENTA_CRASHLOG_EFIVAR;

static size_t get_last_crashlog(efi_system_table* sys, void* ptr, size_t max) {
    efi_runtime_services* rs = sys->RuntimeServices;

    uint32_t attr = MAGENTA_CRASHLOG_EFIATTR;
    size_t sz = max;
    efi_status r = rs->GetVariable(crashlog_name, &magenta_guid, &attr, &sz, ptr);
    if (r == EFI_SUCCESS) {
        // Erase it
        rs->SetVariable(crashlog_name, &magenta_guid, MAGENTA_CRASHLOG_EFIATTR, 0, NULL);
    } else {
        sz = 0;
    }
    return sz;
}

static unsigned char scratch[32768];

static void start_magenta(uint64_t entry, void* bootdata) {
    // ebx = 0, ebp = 0, edi = 0, esi = bootdata
    __asm__ __volatile__(
        "movl $0, %%ebp \n"
        "cli \n"
        "jmp *%[entry] \n" ::[entry] "a"(entry),
        [bootdata] "S"(bootdata),
        "b"(0), "D"(0));
    for (;;)
        ;
}

static int add_bootdata(void** ptr, size_t* avail,
                        bootdata_t* bd, void* data) {
    size_t len = BOOTDATA_ALIGN(bd->length);
    if ((sizeof(bootdata_t) + len) > *avail) {
        printf("boot: no room for bootdata type=%08x size=%08x\n",
               bd->type, bd->length);
        return -1;
    }
    memcpy(*ptr, bd, sizeof(bootdata_t));
    memcpy((*ptr) + sizeof(bootdata_t), data, len);
    len += sizeof(bootdata_t);
    (*ptr) += len;
    (*avail) -= len;
    return 0;
}

int boot_magenta(efi_handle img, efi_system_table* sys,
                 void* image, size_t isz, void* ramdisk, size_t rsz,
                 void* cmdline, size_t csz) {

    efi_boot_services* bs = sys->BootServices;

    magenta_kernel_t* kernel = image;
    if ((isz < sizeof(magenta_kernel_t)) ||
        (kernel->hdr_kernel.type != BOOTDATA_KERNEL)) {
        printf("boot: invalid magenta kernel header\n");
        return -1;
    }

    if ((ramdisk == NULL) || (rsz < sizeof(bootdata_t))) {
        printf("boot: ramdisk missing or too small\n");
        return -1;
    }

    bootdata_t* hdr0 = ramdisk;
    if ((hdr0->type != BOOTDATA_CONTAINER) ||
        (hdr0->extra != BOOTDATA_MAGIC) ||
        (hdr0->flags != 0)) {
        printf("boot: ramdisk has invalid bootdata header\n");
        return -1;
    }
    if ((hdr0->length > (rsz - sizeof(bootdata_t)))) {
        printf("boot: ramdisk has invalid bootdata length\n");
        return -1;
    }

    // osboot ensures we have FRONT_BYTES ahead of the
    // ramdisk to prepend our own bootdata items.
    //
    // We used sizeof(hdr) up front but will overwrite
    // the header at the start of the ramdisk so it works
    // out in the end.

    bootdata_t hdr;
    void* bptr = ramdisk - FRONT_BYTES;
    size_t blen = FRONT_BYTES;

    hdr.type = BOOTDATA_CONTAINER;
    hdr.length = hdr0->length + FRONT_BYTES;
    hdr.extra = BOOTDATA_MAGIC;
    hdr.flags = 0;
    memcpy(bptr, &hdr, sizeof(hdr));
    bptr += sizeof(hdr);

    // pass kernel commandline
    hdr.type = BOOTDATA_CMDLINE;
    hdr.length = csz;
    hdr.extra = 0;
    hdr.flags = 0;
    if (add_bootdata(&bptr, &blen, &hdr, cmdline)) {
        return -1;
    }

    // pass ACPI root pointer
    uint64_t rsdp = find_acpi_root(img, sys);
    hdr.type = BOOTDATA_ACPI_RSDP;
    hdr.length = sizeof(rsdp);
    if (add_bootdata(&bptr, &blen, &hdr, &rsdp)) {
        return -1;
    }

    // pass EFI system table
    uint64_t addr = (uintptr_t) sys;
    hdr.type = BOOTDATA_EFI_SYSTEM_TABLE;
    hdr.length = sizeof(sys);
    if (add_bootdata(&bptr, &blen, &hdr, &addr)) {
        return -1;
    }

    // pass framebuffer data
    efi_graphics_output_protocol* gop = NULL;
    bs->LocateProtocol(&GraphicsOutputProtocol, NULL, (void**)&gop);
    if (gop) {
        bootdata_swfb_t fb = {
            .phys_base = gop->Mode->FrameBufferBase,
            .width = gop->Mode->Info->HorizontalResolution,
            .height = gop->Mode->Info->VerticalResolution,
            .stride = gop->Mode->Info->PixelsPerScanLine,
            .format = get_mx_pixel_format(gop),
        };
        hdr.type = BOOTDATA_FRAMEBUFFER;
        hdr.length = sizeof(fb);
        if (add_bootdata(&bptr, &blen, &hdr, &fb)) {
            return -1;
        }
    }

    // Allocate at 1M and copy kernel down there
    efi_physical_addr mem = 0x100000;
    unsigned pages = BYTES_TO_PAGES(isz);
    //TODO: sort out why pages + 1?  Inherited from deprecated_load()
    if (bs->AllocatePages(AllocateAddress, EfiLoaderData, pages + 1, &mem)) {
        printf("boot: cannot obtain memory @ %p\n", (void*) mem);
        goto fail;
    }
    memcpy((void*)mem, image, isz);

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
    *((uint64_t*) scratch) = dsize;

    // install memory map
    hdr.type = BOOTDATA_EFI_MEMORY_MAP;
    hdr.length = msize + sizeof(uint64_t);
    if (add_bootdata(&bptr, &blen, &hdr, scratch)) {
        goto fail;
    }

    // obtain the last crashlog if we can
    size_t sz = get_last_crashlog(sys, scratch, 4096);
    if (sz > 0) {
        hdr.type = BOOTDATA_LAST_CRASHLOG;
        hdr.length = sz;
        add_bootdata(&bptr, &blen, &hdr, scratch);
    }

    // fill the remaining gap between pre-data and ramdisk image
    if ((blen < sizeof(bootdata_t)) || (blen & 7)) {
        goto fail;
    }
    hdr.type = BOOTDATA_IGNORE;
    hdr.length = blen - sizeof(bootdata_t);
    memcpy(bptr, &hdr, sizeof(hdr));

    // jump to the kernel
    start_magenta(kernel->data_kernel.entry64, ramdisk - FRONT_BYTES);

fail:
    bs->FreePages(mem, pages);
    return -1;
}

static char cmdline[CMDLINE_MAX];

int boot_kernel(efi_handle img, efi_system_table* sys,
                void* image, size_t sz,
                void* ramdisk, size_t rsz) {

    size_t csz = cmdline_to_string(cmdline, sizeof(cmdline));

    bootdata_t* bd = image;
    if ((bd->type == BOOTDATA_CONTAINER) &&
        (bd->extra == BOOTDATA_MAGIC) &&
        (bd->flags == 0)) {
        return boot_magenta(img, sys, image, sz, ramdisk, rsz, cmdline, csz);
    } else {
        return boot_deprecated(img, sys, image, sz, ramdisk, rsz, cmdline, csz);
    }
}
