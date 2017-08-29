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

static bool with_extra = false;
static bootextra_t default_extra = {
    .reserved0 = 0,
    .reserved1 = 0,
    .magic = BOOTITEM_MAGIC,
    .crc32 = BOOTITEM_NO_CRC32,
};

static int add_bootdata(void** ptr, size_t* avail,
                        bootdata_t* bd, void* data) {
    if (with_extra) {
        size_t len = BOOTDATA_ALIGN(bd->length);
        if ((sizeof(bootdata_t) + sizeof(bootextra_t) + len) > *avail) {
            printf("boot: no room for bootdata type=%08x size=%08x\n",
                   bd->type, bd->length);
            return -1;
        }
        bd->flags |= BOOTDATA_FLAG_EXTRA;
        memcpy(*ptr, bd, sizeof(bootdata_t));
        memcpy((*ptr) + sizeof(bootdata_t), &default_extra, sizeof(bootextra_t));
        memcpy((*ptr) + sizeof(bootdata_t) + sizeof(bootextra_t), data, len);
        len += sizeof(bootdata_t) + sizeof(bootextra_t);
        (*ptr) += len;
        (*avail) -= len;
    } else {
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
    }
    return 0;
}

static int header_check(void* image, size_t sz, uint64_t* _entry,
                        size_t* _hsz, size_t* _flen, size_t* _klen) {
    bootdata_t* bd = image;
    size_t hsz, flen, klen;
    uint64_t entry;

    if (bd->flags & BOOTDATA_FLAG_EXTRA) {
        hsz = sizeof(bootdata_t) + sizeof(bootextra_t);
        magenta_kernel2_t* kernel2 = image;
        if ((sz < sizeof(magenta_kernel2_t)) ||
            (kernel2->hdr_kernel.type != BOOTDATA_KERNEL) ||
            ((kernel2->hdr_kernel.flags & BOOTDATA_FLAG_EXTRA) == 0)) {
            printf("boot: invalid magenta kernel header\n");
            return -1;
        }
        flen = BOOTDATA_ALIGN(kernel2->hdr_file.length);
        klen = BOOTDATA_ALIGN(kernel2->hdr_kernel.length);
        entry = kernel2->data_kernel.entry64;
    } else {
        hsz = sizeof(bootdata_t);
        magenta_kernel_t* kernel = image;
        if ((sz < sizeof(magenta_kernel_t)) ||
            (kernel->hdr_kernel.type != BOOTDATA_KERNEL)) {
            printf("boot: invalid magenta kernel header\n");
            return -1;
        }
        flen = BOOTDATA_ALIGN(kernel->hdr_file.length);
        klen = BOOTDATA_ALIGN(kernel->hdr_kernel.length);
        entry = kernel->data_kernel.entry64;
    }

    if (flen > (sz - hsz)) {
        printf("boot: invalid magenta kernel header (bad flen)\n");
        return -1;
    }

    if (klen > (sz - (hsz * 2))) {
        printf("boot: invalid magenta kernel header (bad klen)\n");
        return -1;
    }
    if (_entry) {
        *_entry = entry;
    }
    if (_hsz) {
        *_hsz = hsz;
        *_flen = flen;
        *_klen = klen;
    }

    return 0;
}

int boot_magenta(efi_handle img, efi_system_table* sys,
                 void* image, size_t isz, void* ramdisk, size_t rsz,
                 void* cmdline, size_t csz) {

    efi_boot_services* bs = sys->BootServices;
    uint64_t entry;

    if (header_check(image, isz, &entry, NULL, NULL, NULL)) {
        return -1;
    }
    if ((ramdisk == NULL) || (rsz < (sizeof(bootdata_t) + sizeof(bootextra_t)))) {
        printf("boot: ramdisk missing or too small\n");
        return -1;
    }

    bootdata_t* hdr0 = ramdisk;
    if ((hdr0->type != BOOTDATA_CONTAINER) ||
        (hdr0->extra != BOOTDATA_MAGIC)) {
        printf("boot: ramdisk has invalid bootdata header\n");
        return -1;
    }

    // If the ramdisk container header is a new/large header,
    // generate all our prepended headers in the same style...
    size_t hsz = sizeof(bootdata_t);
    if (hdr0->flags & BOOTDATA_FLAG_EXTRA) {
        with_extra = true;
        hsz += sizeof(bootextra_t);
    }

    if ((hdr0->length > (rsz - hsz))) {
        printf("boot: ramdisk has invalid bootdata length\n");
        return -1;
    }

    // osboot ensures we have FRONT_BYTES ahead of the
    // ramdisk to prepend our own bootdata items.
    bootdata_t hdr;
    void* bptr = ramdisk - FRONT_BYTES;
    size_t blen = FRONT_BYTES;

    // We create a new container header of the same size
    // as the one at the start of the ramdisk
    hdr.type = BOOTDATA_CONTAINER;
    hdr.length = hdr0->length + FRONT_BYTES;
    hdr.extra = BOOTDATA_MAGIC;
    hdr.flags = with_extra ? BOOTDATA_FLAG_EXTRA : 0;
    memcpy(bptr, &hdr, sizeof(hdr));
    bptr += sizeof(hdr);
    if (with_extra) {
        memcpy(bptr, &default_extra, sizeof(default_extra));
        bptr += sizeof(default_extra);
    }

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
            .base = gop->Mode->FrameBufferBase,
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
    memcpy(scratch, &dsize, sizeof(uint64_t));

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
    if ((blen < hsz) || (blen & 7)) {
        goto fail;
    }
    hdr.type = BOOTDATA_IGNORE;
    hdr.length = blen - hsz;
    memcpy(bptr, &hdr, sizeof(hdr));
    if (with_extra) {
        memcpy(bptr + sizeof(hdr), &default_extra, sizeof(default_extra));
    }

    // jump to the kernel
    start_magenta(entry, ramdisk - FRONT_BYTES);

fail:
    bs->FreePages(mem, pages);
    return -1;
}

static char cmdline[CMDLINE_MAX];

int mxboot(efi_handle img, efi_system_table* sys,
           void* image, size_t sz) {

    size_t hsz, flen, klen;
    if (header_check(image, sz, NULL, &hsz, &flen, &klen)) {
        return -1;
    }

    // ramdisk portion is file - headers - kernel len
    uint32_t rlen = flen - hsz - klen;
    uint32_t roff = (hsz * 2) + klen;
    if (rlen == 0) {
        printf("mxboot: no ramdisk?!\n");
        return -1;
    }

    // allocate space for the ramdisk
    efi_boot_services* bs = sys->BootServices;
    size_t rsz = rlen + hsz + FRONT_BYTES;
    size_t pages = BYTES_TO_PAGES(rsz);
    void* ramdisk = NULL;
    efi_status r = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, pages,
                                     (efi_physical_addr*)&ramdisk);
    if (r) {
        printf("mxboot: cannot allocate ramdisk buffer\n");
        return -1;
    }

    ramdisk += FRONT_BYTES;
    bootdata_t* hdr = ramdisk;
    hdr->type = BOOTDATA_CONTAINER;
    hdr->length = rlen;
    hdr->extra = BOOTDATA_MAGIC;
    hdr->flags = 0;
    if (hsz != sizeof(bootdata_t)) {
        hdr->flags |= BOOTDATA_FLAG_EXTRA;
        memcpy(hdr + 1, &default_extra, sizeof(default_extra));
    }
    memcpy(ramdisk + hsz, image + roff, rlen);
    rlen += hsz;

    printf("ramdisk @ %p\n", ramdisk);

    size_t csz = cmdline_to_string(cmdline, sizeof(cmdline));

    // shrink original image header to include only the kernel
    if (hsz == sizeof(bootdata_t)) {
        magenta_kernel_t* kernel = image;
        kernel->hdr_file.length = hsz + klen;
    } else {
        magenta_kernel2_t* kernel2 = image;
        kernel2->hdr_file.length = hsz + klen;
    }

    return boot_magenta(img, sys, image, roff, ramdisk, rlen, cmdline, csz);
}

int boot_kernel(efi_handle img, efi_system_table* sys,
                void* image, size_t sz,
                void* ramdisk, size_t rsz) {

    size_t csz = cmdline_to_string(cmdline, sizeof(cmdline));

    bootdata_t* bd = image;
    if ((bd->type == BOOTDATA_CONTAINER) &&
        (bd->extra == BOOTDATA_MAGIC)) {
        return boot_magenta(img, sys, image, sz, ramdisk, rsz, cmdline, csz);
    } else {
        return -1;
    }
}
