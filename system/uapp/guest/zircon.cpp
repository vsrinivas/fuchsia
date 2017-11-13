// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/guest.h>
#include <zircon/assert.h>
#include <zircon/boot/bootdata.h>

#include "zircon.h"

static const uintptr_t kKernelOffset = 0x100000;
static const uintptr_t kBootdataOffset = 0x800000;
static const uint16_t kMzSignature = 0x5a4d; // MZ
static const uint32_t kMzMagic = 0x644d5241; // ARM\x64

// MZ header used to boot ARM64 kernels.
//
// See: https://www.kernel.org/doc/Documentation/arm64/booting.txt.
struct MzHeader {
    uint32_t code0;
    uint32_t code1;
    uint64_t kernel_off;
    uint64_t kernel_len;
    uint64_t flags;
    uint64_t reserved0;
    uint64_t reserved1;
    uint64_t reserved2;
    uint32_t magic;
    uint32_t pe_off;
} __PACKED;
static_assert(sizeof(MzHeader) == 64, "");

static bool is_mz(const MzHeader* header) {
    return (header->code0 & UINT16_MAX) == kMzSignature &&
           header->kernel_len > sizeof(MzHeader) &&
           header->magic == kMzMagic &&
           header->pe_off >= sizeof(MzHeader);
}

static bool is_bootdata(const bootdata_t* header) {
    return header->type == BOOTDATA_CONTAINER &&
           header->length > sizeof(bootdata_t) &&
           header->extra == BOOTDATA_MAGIC &&
           header->flags & BOOTDATA_FLAG_V2 &&
           header->magic == BOOTITEM_MAGIC;
}

static void set_bootdata(bootdata_t* header, uint32_t type, uint32_t len) {
    // Guest memory is initially zeroed, so we skip fields that must be zero.
    header->type = type;
    header->length = len;
    header->flags = BOOTDATA_FLAG_V2;
    header->magic = BOOTITEM_MAGIC;
    header->crc32 = BOOTITEM_NO_CRC32;
}

static zx_status_t load_zircon(const int fd, const uintptr_t addr, const uintptr_t first_page,
                               const uintptr_t kernel_off, const uintptr_t kernel_len) {
    // Move the first page to the kernel offset.
    memmove(reinterpret_cast<void*>(addr + kernel_off), reinterpret_cast<void*>(first_page),
            PAGE_SIZE);

    // Read in the rest of the kernel.
    const uintptr_t data_off = kernel_off + PAGE_SIZE;
    const size_t data_len = kernel_len - PAGE_SIZE;
    const ssize_t ret = read(fd, reinterpret_cast<void*>(addr + data_off), data_len);
    if (ret < 0 || (size_t)ret != data_len) {
        fprintf(stderr, "Failed to read Zircon kernel data\n");
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

static zx_status_t load_cmdline(const char* cmdline, const uintptr_t addr,
                                const uintptr_t bootdata_off) {
    const size_t cmdline_len = strlen(cmdline) + 1;
    if (cmdline_len > UINT32_MAX) {
        fprintf(stderr, "Command line is too long\n");
        return ZX_ERR_OUT_OF_RANGE;
    }

    bootdata_t* container_hdr = reinterpret_cast<bootdata_t*>(addr + bootdata_off);
    uintptr_t data_off = bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(container_hdr->length);

    bootdata_t* cmdline_hdr = reinterpret_cast<bootdata_t*>(addr + data_off);
    set_bootdata(cmdline_hdr, BOOTDATA_CMDLINE, static_cast<uint32_t>(cmdline_len));
    memcpy(cmdline_hdr + 1, cmdline, cmdline_len);

    container_hdr->length += static_cast<uint32_t>(sizeof(bootdata_t)) +
                             BOOTDATA_ALIGN(cmdline_hdr->length);
    return ZX_OK;
}

static zx_status_t load_bootfs(const int fd, const uintptr_t addr, const uintptr_t bootdata_off) {
    bootdata_t ramdisk_hdr;
    ssize_t ret = read(fd, &ramdisk_hdr, sizeof(bootdata_t));
    if (ret != sizeof(bootdata_t)) {
        fprintf(stderr, "Failed to read BOOTFS image header\n");
        return ZX_ERR_IO;
    }
    if (!is_bootdata(&ramdisk_hdr)) {
        fprintf(stderr, "Invalid BOOTFS image header\n");
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    bootdata_t* container_hdr = reinterpret_cast<bootdata_t*>(addr + bootdata_off);
    uintptr_t data_off = bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(container_hdr->length);

    ret = read(fd, reinterpret_cast<void*>(addr + data_off), ramdisk_hdr.length);
    if (ret < 0 || (size_t)ret != ramdisk_hdr.length) {
        fprintf(stderr, "Failed to read BOOTFS image data\n");
        return ZX_ERR_IO;
    }

    container_hdr->length += BOOTDATA_ALIGN(ramdisk_hdr.length) +
                            static_cast<uint32_t>(sizeof(bootdata_t));
    return ZX_OK;
}

static zx_status_t create_bootdata(const uintptr_t addr, const size_t size,
                                   const uintptr_t acpi_off, uintptr_t bootdata_off) {
    if (BOOTDATA_ALIGN(bootdata_off) != bootdata_off)
        return ZX_ERR_INVALID_ARGS;

    const size_t e820_size = guest_e820_size(size);
    const size_t bootdata_len = sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(uint64_t)) +
                                sizeof(bootdata_t) + BOOTDATA_ALIGN(e820_size);
    if (bootdata_off + bootdata_len > size)
        return ZX_ERR_BUFFER_TOO_SMALL;
    if (bootdata_len > UINT32_MAX)
        return ZX_ERR_OUT_OF_RANGE;

    // Bootdata container.
    bootdata_t* container_hdr = reinterpret_cast<bootdata_t*>(addr + bootdata_off);
    set_bootdata(container_hdr, BOOTDATA_CONTAINER, static_cast<uint32_t>(bootdata_len));
    container_hdr->extra = BOOTDATA_MAGIC;

    // ACPI root table pointer.
    bootdata_off += sizeof(bootdata_t);
    bootdata_t* acpi_rsdp_hdr = reinterpret_cast<bootdata_t*>(addr + bootdata_off);
    set_bootdata(acpi_rsdp_hdr, BOOTDATA_ACPI_RSDP, sizeof(uint64_t));
    bootdata_off += sizeof(bootdata_t);
    *reinterpret_cast<uint64_t*>(addr + bootdata_off) = acpi_off;

    // E820 memory map.
    bootdata_off += BOOTDATA_ALIGN(sizeof(uint64_t));
    bootdata_t* e820_table_hdr = reinterpret_cast<bootdata_t*>(addr + bootdata_off);
    set_bootdata(e820_table_hdr, BOOTDATA_E820_TABLE, static_cast<uint32_t>(e820_size));
    bootdata_off += sizeof(bootdata_t);
    return guest_create_e820(addr, size, bootdata_off);
}

static zx_status_t is_zircon(const size_t size, const uintptr_t first_page, uintptr_t* guest_ip,
                             uintptr_t* kernel_off, uintptr_t* kernel_len) {
    zircon_kernel_t* kernel_header = reinterpret_cast<zircon_kernel_t*>(first_page);
    if (is_bootdata(&kernel_header->hdr_file)) {
        if (kernel_header->hdr_kernel.type != BOOTDATA_KERNEL) {
            fprintf(stderr, "Invalid Zircon kernel header\n");
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
        *guest_ip = kernel_header->data_kernel.entry64;
        *kernel_off = kKernelOffset;
        *kernel_len = sizeof(bootdata_t) + BOOTDATA_ALIGN(kernel_header->hdr_file.length);
        return ZX_OK;
    }

#if __aarch64__
    MzHeader* mz_header = reinterpret_cast<MzHeader*>(first_page);
    if (is_mz(mz_header)) {
        *guest_ip = mz_header->kernel_off;
        *kernel_off = mz_header->kernel_off;
        *kernel_len = mz_header->kernel_len;
        return ZX_OK;
    }
#endif

    return ZX_ERR_NOT_SUPPORTED;
}

static bool is_within(uintptr_t x, uintptr_t addr, uintptr_t size) {
    return x >= addr && x < addr + size;
}

zx_status_t setup_zircon(const uintptr_t addr, const size_t size, const uintptr_t first_page,
                         const uintptr_t acpi_off, const int fd, const char* bootdata_path,
                         const char* cmdline, uintptr_t* guest_ip, uintptr_t* bootdata_off) {
    uintptr_t kernel_off = 0;
    uintptr_t kernel_len = 0;
    zx_status_t status = is_zircon(size, first_page, guest_ip, &kernel_off, &kernel_len);
    if (status != ZX_OK)
        return status;

    if (!is_within(*guest_ip, kernel_off, kernel_len)) {
        fprintf(stderr, "Kernel entry point is outside of kernel location\n");
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (kernel_off + kernel_len >= size) {
        fprintf(stderr, "Kernel location is outside of guest physical memory\n");
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (is_within(kBootdataOffset, kernel_off, kernel_len)) {
        fprintf(stderr, "Kernel location overlaps BOOTFS location\n");
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    status = create_bootdata(addr, size, acpi_off, kBootdataOffset);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create BOOTDATA\n");
        return status;
    }

    status = load_zircon(fd, addr, first_page, kernel_off, kernel_len);
    if (status != ZX_OK)
        return status;

    // If we have a command line, load it.
    if (cmdline != NULL) {
        status = load_cmdline(cmdline, addr, kBootdataOffset);
        if (status != ZX_OK)
            return status;
    }

    // If we have been provided a BOOTFS image, load it.
    if (bootdata_path) {
        int boot_fd = open(bootdata_path, O_RDONLY);
        if (boot_fd < 0) {
            fprintf(stderr, "Failed to open BOOTFS image \"%s\"\n", bootdata_path);
            return ZX_ERR_IO;
        }

        status = load_bootfs(boot_fd, addr, kBootdataOffset);
        close(boot_fd);
        if (status != ZX_OK)
            return status;
    }

    *bootdata_off = kBootdataOffset;
    return ZX_OK;
}
