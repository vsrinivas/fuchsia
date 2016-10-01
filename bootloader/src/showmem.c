// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/types.h>
#include <efi/system-table.h>

#include <printf.h>

static const char* MemTypeName(uint32_t type, char* buf) {
    switch (type) {
    case EfiReservedMemoryType:
        return "Reserved";
    case EfiLoaderCode:
        return "LoaderCode";
    case EfiLoaderData:
        return "LoaderData";
    case EfiBootServicesCode:
        return "BootSvcsCode";
    case EfiBootServicesData:
        return "BootSvcsData";
    case EfiRuntimeServicesCode:
        return "RunTimeCode";
    case EfiRuntimeServicesData:
        return "RunTimeData";
    case EfiConventionalMemory:
        return "Conventional";
    case EfiUnusableMemory:
        return "Unusable";
    case EfiACPIReclaimMemory:
        return "ACPIReclaim";
    case EfiACPIMemoryNVS:
        return "ACPINonVolMem";
    case EfiMemoryMappedIO:
        return "MemMappedIO";
    case EfiMemoryMappedIOPortSpace:
        return "MemMappedPort";
    case EfiPalCode:
        return "PalCode";
    default:
        sprintf(buf, "0x%08x", type);
        return buf;
    }
}

static unsigned char scratch[4096];

static void dump_memmap(efi_system_table* systab) {
    efi_status r;
    size_t msize, off;
    efi_memory_descriptor* mmap;
    size_t mkey, dsize;
    uint32_t dversion;
    char tmp[32];

    msize = sizeof(scratch);
    mmap = (efi_memory_descriptor*)scratch;
    mkey = dsize = dversion;
    r = systab->BootServices->GetMemoryMap(&msize, mmap, &mkey, &dsize, &dversion);
    printf("r=%lx msz=%lx key=%lx dsz=%lx dvn=%x\n",
           r, msize, mkey, dsize, dversion);
    if (r != EFI_SUCCESS) {
        return;
    }
    for (off = 0; off < msize; off += dsize) {
        mmap = (efi_memory_descriptor*)(scratch + off);
        printf("%016lx %016lx %08lx %c %04lx %s\n",
               mmap->PhysicalStart, mmap->VirtualStart,
               mmap->NumberOfPages,
               mmap->Attribute & EFI_MEMORY_RUNTIME ? 'R' : '-',
               mmap->Attribute & 0xFFFF,
               MemTypeName(mmap->Type, tmp));
    }
}

#include <xefi.h>

EFIAPI efi_status efi_main(efi_handle img, efi_system_table* sys) {
    xefi_init(img, sys);
    dump_memmap(sys);
    xefi_wait_any_key();
    return EFI_SUCCESS;
}
