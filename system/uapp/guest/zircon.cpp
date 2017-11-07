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
    uint64_t kernel_offset;
    uint64_t kernel_length;
    uint64_t flags;
    uint64_t reserved0;
    uint64_t reserved1;
    uint64_t reserved2;
    uint32_t magic;
    uint32_t pe_offset;
} __PACKED;
static_assert(sizeof(MzHeader) == 64, "");

static bool is_mz(const MzHeader* header) {
    return (header->code0 & UINT16_MAX) == kMzSignature &&
           header->kernel_length > sizeof(MzHeader) &&
           header->magic == kMzMagic &&
           header->pe_offset >= sizeof(MzHeader);
}

static bool is_bootdata(const bootdata_t* container) {
    return container->type == BOOTDATA_CONTAINER &&
           container->length > sizeof(bootdata_t) &&
           container->extra == BOOTDATA_MAGIC &&
           container->flags == 0;
}

static zx_status_t load_zircon(const int fd, const uintptr_t addr, const size_t size,
                               const uintptr_t first_page, const uintptr_t kernel_offset,
                               const uintptr_t kernel_length) {
    zircon_kernel_t* header = reinterpret_cast<zircon_kernel_t*>(addr + kernel_offset);
    // Move the first page to where zircon would like it to be
    memmove(header, reinterpret_cast<void*>(first_page), PAGE_SIZE);

    // We already read a page, now we need the rest...
    // The rest is the length in the header, minus what we already read, but accounting for
    // the bootdata_kernel_t portion of zircon_kernel_t that's included in the header length.
    const uintptr_t data_off = kernel_offset + PAGE_SIZE;
    const size_t data_len = kernel_length - PAGE_SIZE;
    const ssize_t ret = read(fd, reinterpret_cast<void*>(addr + data_off), data_len);
    if (ret < 0 || (size_t)ret != data_len) {
        fprintf(stderr, "Failed to read Zircon kernel data\n");
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

static zx_status_t load_cmdline(const char* cmdline, const uintptr_t addr,
                                const uintptr_t bootdata_off) {
    bootdata_t* bootdata_hdr = (bootdata_t*)(addr + bootdata_off);
    uintptr_t data_off = bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(bootdata_hdr->length);

    bootdata_t* cmdline_hdr = (bootdata_t*)(addr + data_off);
    cmdline_hdr->type = BOOTDATA_CMDLINE;
    size_t cmdline_len = strlen(cmdline) + 1;
    if (cmdline_len > UINT32_MAX) {
        fprintf(stderr, "Command line length is outside of 32-bit range\n");
        return ZX_ERR_OUT_OF_RANGE;
    }
    cmdline_hdr->length = cmdline_len & UINT32_MAX;
    memcpy(cmdline_hdr + 1, cmdline, cmdline_len);

    bootdata_hdr->length += cmdline_hdr->length + static_cast<uint32_t>(sizeof(bootdata_t));
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
        fprintf(stderr, "Invalid BOOTFS container\n");
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    bootdata_t* bootdata_hdr = (bootdata_t*)(addr + bootdata_off);
    uintptr_t data_off = bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(bootdata_hdr->length);
    ret = read(fd, (void*)(addr + data_off), ramdisk_hdr.length);
    if (ret < 0 || (size_t)ret != ramdisk_hdr.length) {
        fprintf(stderr, "Failed to read BOOTFS image data\n");
        return ZX_ERR_IO;
    }

    bootdata_hdr->length += ramdisk_hdr.length + static_cast<uint32_t>(sizeof(bootdata_t));
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
    bootdata_t* header = (bootdata_t*)(addr + bootdata_off);
    header->type = BOOTDATA_CONTAINER;
    header->extra = BOOTDATA_MAGIC;
    header->length = static_cast<uint32_t>(bootdata_len);

    // ACPI root table pointer.
    bootdata_off += sizeof(bootdata_t);
    bootdata_t* bootdata = (bootdata_t*)(addr + bootdata_off);
    bootdata->type = BOOTDATA_ACPI_RSDP;
    bootdata->length = sizeof(uint64_t);

    bootdata_off += sizeof(bootdata_t);
    uint64_t* acpi_rsdp = (uint64_t*)(addr + bootdata_off);
    *acpi_rsdp = acpi_off;

    // E820 memory map.
    bootdata_off += BOOTDATA_ALIGN(sizeof(uint64_t));
    bootdata = (bootdata_t*)(addr + bootdata_off);
    bootdata->type = BOOTDATA_E820_TABLE;
    bootdata->length = static_cast<uint32_t>(e820_size);

    bootdata_off += sizeof(bootdata_t);
    return guest_create_e820(addr, size, bootdata_off);
}

static zx_status_t is_zircon(const size_t size, const uintptr_t first_page, uintptr_t* guest_ip,
                             uintptr_t* kernel_offset, uintptr_t* kernel_length) {
    zircon_kernel_t* kernel_header = reinterpret_cast<zircon_kernel_t*>(first_page);
    if (is_bootdata(&kernel_header->hdr_file)) {
        if (kernel_header->hdr_kernel.type != BOOTDATA_KERNEL) {
            fprintf(stderr, "Invalid Zircon kernel header\n");
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
        if (kernel_header->data_kernel.entry64 >= size) {
            fprintf(stderr, "Kernel entry point is outside of guest physical memory\n");
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
        *guest_ip = kernel_header->data_kernel.entry64;
        *kernel_offset = kKernelOffset;
        *kernel_length = kernel_header->hdr_kernel.length + sizeof(zircon_kernel_t) -
                         sizeof(bootdata_kernel_t);
        return ZX_OK;
    }

#if __aarch64__
    MzHeader* mz_header = reinterpret_cast<MzHeader*>(first_page);
    if (is_mz(mz_header)) {
        *guest_ip = mz_header->kernel_offset;
        *kernel_offset = mz_header->kernel_offset;
        *kernel_length = mz_header->kernel_length;
        return ZX_OK;
    }
#endif

    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t setup_zircon(const uintptr_t addr, const size_t size, const uintptr_t first_page,
                         const uintptr_t acpi_off, const int fd, const char* bootdata_path,
                         const char* cmdline, uintptr_t* guest_ip, uintptr_t* bootdata_offset) {
    uintptr_t kernel_offset = 0;
    uintptr_t kernel_length = 0;
    zx_status_t status = is_zircon(size, first_page, guest_ip, &kernel_offset, &kernel_length);
    if (status != ZX_OK)
        return status;

    status = create_bootdata(addr, size, acpi_off, kBootdataOffset);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create bootdata\n");
        return status;
    }

    status = load_zircon(fd, addr, size, first_page, kernel_offset, kernel_length);
    if (status != ZX_OK)
        return status;
    ZX_ASSERT(kernel_offset + kernel_length <= kBootdataOffset);

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
    *bootdata_offset = kBootdataOffset;
    return ZX_OK;
}
