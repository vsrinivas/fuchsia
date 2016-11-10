// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta.h>

#include <efi/protocol/graphics-output.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <xefi.h>

#include <magenta/pixelformat.h>

static efi_guid AcpiTableGUID = ACPI_TABLE_GUID;
static efi_guid Acpi2TableGUID = ACPI_20_TABLE_GUID;
static uint8_t ACPI_RSD_PTR[8] = "RSD PTR ";

uint32_t find_acpi_root(efi_handle img, efi_system_table* sys) {
    efi_configuration_table* cfgtab = sys->ConfigurationTable;
    int i;

    for (i = 0; i < sys->NumberOfTableEntries; i++) {
        if (!xefi_cmp_guid(&cfgtab[i].VendorGuid, &AcpiTableGUID) &&
            !xefi_cmp_guid(&cfgtab[i].VendorGuid, &Acpi2TableGUID)) {
            // not an ACPI table
            continue;
        }
        if (memcmp(cfgtab[i].VendorTable, ACPI_RSD_PTR, 8)) {
            // not the Root Description Pointer
            continue;
        }
        return (uint64_t)cfgtab[i].VendorTable;
    }
    return 0;
}

#define E820_IGNORE 0
#define E820_RAM 1
#define E820_RESERVED 2
#define E820_ACPI 3
#define E820_NVS 4
#define E820_UNUSABLE 5

static inline const char* e820name(int e820) {
    switch (e820) {
    case E820_IGNORE:   return "IGNORE";
    case E820_RAM:      return "RAM";
    case E820_RESERVED: return "RESERVED";
    case E820_ACPI:     return "ACPI";
    case E820_NVS:      return "NVS";
    case E820_UNUSABLE: return "UNUSABLE";
    }
    return "";
}

struct e820entry {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} __attribute__((packed));

static unsigned e820type(unsigned uefi_mem_type) {
    switch (uefi_mem_type) {
    case EfiReservedMemoryType:
    case EfiPalCode:
        return E820_RESERVED;
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
#if WITH_RUNTIME_SERVICES
        return E820_RESERVED;
#else
        return E820_RAM;
#endif
    case EfiACPIReclaimMemory:
        return E820_ACPI;
    case EfiACPIMemoryNVS:
        return E820_NVS;
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiConventionalMemory:
        return E820_RAM;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
        return E820_IGNORE;
    default:
        if (uefi_mem_type >= 0x80000000) {
            return E820_RAM;
        }
        return E820_UNUSABLE;
    }
}

static unsigned char scratch[32768];
static struct e820entry e820table[128];

static int process_memory_map(efi_system_table* sys, size_t* _key, int silent) {
    efi_memory_descriptor* mmap;
    struct e820entry* entry = e820table;
    size_t msize, off;
    size_t mkey, dsize;
    uint32_t dversion;
    unsigned n, type;
    efi_status r;

    msize = sizeof(scratch);
    mmap = (efi_memory_descriptor*)scratch;
    mkey = dsize = dversion = 0;
    r = sys->BootServices->GetMemoryMap(&msize, mmap, &mkey, &dsize, &dversion);
    if (!silent)
        printf("r=%zx msz=%zx key=%zx dsz=%zx dvn=%x\n", r, msize, mkey, dsize, dversion);
    if (r != EFI_SUCCESS) {
        return -1;
    }
    if (msize > sizeof(scratch)) {
        if (!silent)
            printf("Memory Table Too Large (%zu entries)\n", (msize / dsize));
        return -1;
    }
    for (off = 0, n = 0; off < msize; off += dsize) {
        mmap = (efi_memory_descriptor*)(scratch + off);
        type = e820type(mmap->Type);
        if (type == E820_IGNORE) {
            continue;
        }
        if ((n > 0) && (entry[n - 1].type == type)) {
            if ((entry[n - 1].addr + entry[n - 1].size) == mmap->PhysicalStart) {
                entry[n - 1].size += mmap->NumberOfPages * 4096UL;
                continue;
            }
        }
        entry[n].addr = mmap->PhysicalStart;
        entry[n].size = mmap->NumberOfPages * 4096UL;
        entry[n].type = type;
        n++;
        if (n == 128) {
            if (!silent)
                printf("E820 Table Too Large (%zu raw entries)\n", (msize / dsize));
            return -1;
        }
    }
    *_key = mkey;
    return n;
}

#define ZP_E820_COUNT 0x1E8   // byte
#define ZP_SETUP 0x1F1        // start of setup structure
#define ZP_SETUP_SECTS 0x1F1  // byte (setup_size/512-1)
#define ZP_JUMP 0x200         // jump instruction
#define ZP_HEADER 0x202       // word "HdrS"
#define ZP_VERSION 0x206      // half 0xHHLL
#define ZP_LOADER_TYPE 0x210  // byte
#define ZP_RAMDISK_BASE 0x218 // word (ptr or 0)
#define ZP_RAMDISK_SIZE 0x21C // word (bytes)
#define ZP_EXTRA_MAGIC 0x220  // word
#define ZP_CMDLINE 0x228      // word (ptr)
#define ZP_SYSSIZE 0x1F4      // word (size/16)
#define ZP_XLOADFLAGS 0x236   // half
#define ZP_E820_TABLE 0x2D0   // 128 entries

#define ZP_ACPI_RSD 0x080 // word phys ptr
#define ZP_FB_BASE 0x090
#define ZP_FB_WIDTH 0x094
#define ZP_FB_HEIGHT 0x098
#define ZP_FB_STRIDE 0x09C
#define ZP_FB_FORMAT 0x0A0
#define ZP_FB_REGBASE 0x0A4
#define ZP_FB_SIZE 0x0A8

#define ZP_MAGIC_VALUE 0xDBC64323

#define ZP8(p, off) (*((uint8_t*)((p) + (off))))
#define ZP16(p, off) (*((uint16_t*)((p) + (off))))
#define ZP32(p, off) (*((uint32_t*)((p) + (off))))

static void install_memmap(kernel_t* k, struct e820entry* memmap, unsigned count) {
    memcpy(k->zeropage + ZP_E820_TABLE, memmap, sizeof(*memmap) * count);
    ZP8(k->zeropage, ZP_E820_COUNT) = count;
}

static void get_bit_range(uint32_t mask, int* high, int* low) {
    *high = -1;
    *low = -1;
    int idx = 0;
    while (mask) {
        if (*low < 0 && (mask & 1)) *low = idx;
        idx++;
        mask >>= 1;
    }
    *high = idx - 1;
}

static int get_mx_pixel_format_from_bitmask(efi_pixel_bitmask bitmask) {
    int r_hi = -1, r_lo = -1, g_hi = -1, g_lo = -1, b_hi = -1, b_lo = -1;

    get_bit_range(bitmask.RedMask, &r_hi, &r_lo);
    get_bit_range(bitmask.GreenMask, &g_hi, &g_lo);
    get_bit_range(bitmask.BlueMask, &b_hi, &b_lo);

    if (r_lo < 0 || g_lo < 0 || b_lo < 0) {
        goto unsupported;
    }

    if ((r_hi == 23 && r_lo == 16) &&
        (g_hi == 15 && g_lo == 8) &&
        (b_hi == 7 && b_lo == 0)) {
        return MX_PIXEL_FORMAT_RGB_x888;
    }

    if ((r_hi == 7 && r_lo == 5) &&
        (g_hi == 4 && g_lo == 2) &&
        (b_hi == 1 && b_lo == 0)) {
        return MX_PIXEL_FORMAT_RGB_332;
    }

    if ((r_hi == 15 && r_lo == 11) &&
        (g_hi == 10 && g_lo == 5) &&
        (b_hi == 4 && b_lo == 0)) {
        return MX_PIXEL_FORMAT_RGB_565;
    }

    if ((r_hi == 7 && r_lo == 6) &&
        (g_hi == 5 && g_lo == 4) &&
        (b_hi == 3 && b_lo == 2)) {
        return MX_PIXEL_FORMAT_RGB_2220;
    }

unsupported:
    printf("unsupported pixel format bitmask: r %08x / g %08x / b %08x\n",
            bitmask.RedMask, bitmask.GreenMask, bitmask.BlueMask);
    return MX_PIXEL_FORMAT_NONE;
}

static int get_mx_pixel_format(efi_graphics_output_protocol* gop) {
    efi_graphics_pixel_format efi_fmt = gop->Mode->Info->PixelFormat;
    switch (efi_fmt) {
    case PixelBlueGreenRedReserved8BitPerColor:
        return MX_PIXEL_FORMAT_RGB_x888;
    case PixelBitMask:
        return get_mx_pixel_format_from_bitmask(gop->Mode->Info->PixelInformation);
    default:
        printf("unsupported pixel format %d!\n", efi_fmt);
        return MX_PIXEL_FORMAT_NONE;
    }
}

static void start_kernel(kernel_t* k) {
    // 64bit entry is at offset 0x200
    uint64_t entry = (uint64_t)(k->image + 0x200);

    // ebx = 0, ebp = 0, edi = 0, esi = zeropage
    __asm__ __volatile__(
        "movl $0, %%ebp \n"
        "cli \n"
        "jmp *%[entry] \n" ::[entry] "a"(entry),
        [zeropage] "S"(k->zeropage),
        "b"(0), "D"(0));
    for (;;)
        ;
}

static int load_kernel(efi_boot_services* bs, uint8_t* image, size_t sz, kernel_t* k) {
    uint32_t setup_sz;
    uint32_t image_sz;
    uint32_t setup_end;
    efi_physical_addr mem;

    k->zeropage = NULL;
    k->cmdline = NULL;
    k->image = NULL;
    k->pages = 0;

    if (sz < 1024) {
        // way too small to be a kernel
        goto fail;
    }

    if (ZP32(image, ZP_HEADER) != 0x53726448) {
        printf("kernel: invalid setup magic %08x\n", ZP32(image, ZP_HEADER));
        goto fail;
    }
    if (ZP16(image, ZP_VERSION) < 0x020B) {
        printf("kernel: unsupported setup version %04x\n", ZP16(image, ZP_VERSION));
        goto fail;
    }
    setup_sz = (ZP8(image, ZP_SETUP_SECTS) + 1) * 512;
    image_sz = (ZP32(image, ZP_SYSSIZE) * 16);
    setup_end = ZP_JUMP + ZP8(image, ZP_JUMP + 1);

    printf("setup %d image %d  hdr %04x-%04x\n", setup_sz, image_sz, ZP_SETUP, setup_end);
    // image size may be rounded up, thus +15
    if ((setup_sz < 1024) || ((setup_sz + image_sz) > (sz + 15))) {
        printf("kernel: invalid image size\n");
        goto fail;
    }

    mem = 0xFF000;
    if (bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1, &mem)) {
        printf("kernel: cannot allocate 'zero page'\n");
        goto fail;
    }
    k->zeropage = (void*)mem;

    mem = 0xFF000;
    if (bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1, &mem)) {
        printf("kernel: cannot allocate commandline\n");
        goto fail;
    }
    k->cmdline = (void*)mem;

    mem = 0x100000;
    k->pages = (image_sz + 4095) / 4096;
    if (bs->AllocatePages(AllocateAddress, EfiLoaderData, k->pages + 1, &mem)) {
        printf("kernel: cannot allocate kernel\n");
        goto fail;
    }
    k->image = (void*)mem;

    // setup zero page, copy setup header from kernel binary
    memset(k->zeropage, 0, 4096);
    memcpy(k->zeropage + ZP_SETUP, image + ZP_SETUP, setup_end - ZP_SETUP);

    memcpy(k->image, image + setup_sz, image_sz);

    // empty commandline for now
    ZP32(k->zeropage, ZP_CMDLINE) = (uint64_t)k->cmdline;
    k->cmdline[0] = 0;

    // default to no ramdisk
    ZP32(k->zeropage, ZP_RAMDISK_BASE) = 0;
    ZP32(k->zeropage, ZP_RAMDISK_SIZE) = 0;

    // undefined bootloader
    ZP8(k->zeropage, ZP_LOADER_TYPE) = 0xFF;

    printf("kernel @%p, zeropage @%p, cmdline @%p\n",
           k->image, k->zeropage, k->cmdline);

    return 0;
fail:
    if (k->image) {
        bs->FreePages((efi_physical_addr)k->image, k->pages);
    }
    if (k->cmdline) {
        bs->FreePages((efi_physical_addr)k->cmdline, 1);
    }
    if (k->zeropage) {
        bs->FreePages((efi_physical_addr)k->zeropage, 1);
    }

    return -1;
}

int boot_kernel(efi_handle img, efi_system_table* sys,
                void* image, size_t sz, void* ramdisk, size_t rsz,
                void* cmdline, size_t csz, void* cmdline2, size_t csz2) {
    efi_boot_services* bs = sys->BootServices;
    kernel_t kernel;
    efi_status r;
    size_t key;
    int n, i;

    efi_graphics_output_protocol* gop;
    bs->LocateProtocol(&GraphicsOutputProtocol, NULL, (void**)&gop);

    printf("boot_kernel() from %p (%zu bytes)\n", image, sz);
    if (ramdisk && rsz) {
        printf("ramdisk at %p (%zu bytes)\n", ramdisk, rsz);
    }

    if (load_kernel(sys->BootServices, image, sz, &kernel)) {
        printf("Failed to load kernel image\n");
        return -1;
    }

    ZP32(kernel.zeropage, ZP_EXTRA_MAGIC) = ZP_MAGIC_VALUE;
    ZP32(kernel.zeropage, ZP_ACPI_RSD) = find_acpi_root(img, sys);

    ZP32(kernel.zeropage, ZP_FB_BASE) = (uint32_t)gop->Mode->FrameBufferBase;
    ZP32(kernel.zeropage, ZP_FB_WIDTH) = (uint32_t)gop->Mode->Info->HorizontalResolution;
    ZP32(kernel.zeropage, ZP_FB_HEIGHT) = (uint32_t)gop->Mode->Info->VerticalResolution;
    ZP32(kernel.zeropage, ZP_FB_STRIDE) = (uint32_t)gop->Mode->Info->PixelsPerScanLine;
    ZP32(kernel.zeropage, ZP_FB_FORMAT) = get_mx_pixel_format(gop);
    ZP32(kernel.zeropage, ZP_FB_REGBASE) = 0;
    ZP32(kernel.zeropage, ZP_FB_SIZE) = 256 * 1024 * 1024;

    if ((csz == 0) && (csz2 != 0)) {
        cmdline = cmdline2;
        csz = csz2;
        csz2 = 0;
    }
    if (cmdline) {
        // Truncate the cmdline to fit on a page
        if (csz >= 4095) {
            csz = 4095;
        }
        memcpy(kernel.cmdline, cmdline, csz);
        if (cmdline2 && (csz2 < (4095 - csz))) {
            memcpy(kernel.cmdline + csz, cmdline2, csz2);
            csz += csz2;
        }
        kernel.cmdline[csz] = '\0';
    }

    if (ramdisk && rsz) {
        ZP32(kernel.zeropage, ZP_RAMDISK_BASE) = (uint32_t) (uintptr_t) ramdisk;
        ZP32(kernel.zeropage, ZP_RAMDISK_SIZE) = rsz;
    }
    n = process_memory_map(sys, &key, 0);

    for (i = 0; i < n; i++) {
        struct e820entry* e = e820table + i;
        printf("%016" PRIx64 " %016" PRIx64 " %s\n",
               e->addr, e->size, e820name(e->type));
    }

    r = sys->BootServices->ExitBootServices(img, key);
    if (r == EFI_INVALID_PARAMETER) {
        n = process_memory_map(sys, &key, 1);
        r = sys->BootServices->ExitBootServices(img, key);
        if (r) {
            printf("Cannot ExitBootServices! (2) %s\n", xefi_strerror(r));
            return -1;
        }
    } else if (r) {
        printf("Cannot ExitBootServices! (1) %s\n", xefi_strerror(r));
        return -1;
    }

    install_memmap(&kernel, e820table, n);
    start_kernel(&kernel);

    return 0;
}
