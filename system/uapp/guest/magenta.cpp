// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/guest.h>
#include <magenta/assert.h>
#include <magenta/boot/bootdata.h>

#include "magenta.h"

static const uintptr_t kKernelOffset = 0x100000;
static const uintptr_t kBootdataOffset = 0x800000;

static bool container_is_valid(const bootdata_t* container) {
    return container->type == BOOTDATA_CONTAINER &&
           container->length > sizeof(bootdata_t) &&
           container->extra == BOOTDATA_MAGIC &&
           container->flags == 0;
}

static mx_status_t load_magenta(const int fd, const uintptr_t addr, const size_t size,
                                const uintptr_t first_page, uintptr_t* guest_ip,
                                uintptr_t* end_off) {
    magenta_kernel_t* header = reinterpret_cast<magenta_kernel_t*>(addr + kKernelOffset);
    // Move the first page to where magenta would like it to be
    memmove(header, reinterpret_cast<void*>(first_page), PAGE_SIZE);

    if (!container_is_valid(&header->hdr_file)) {
        fprintf(stderr, "Invalid Magenta container\n");
        return MX_ERR_IO_DATA_INTEGRITY;
    }
    if (header->hdr_kernel.type != BOOTDATA_KERNEL) {
        fprintf(stderr, "Invalid Magenta kernel header\n");
        return MX_ERR_IO_DATA_INTEGRITY;
    }
    if (header->data_kernel.entry64 >= size) {
        fprintf(stderr, "Kernel entry point is outside of guest physical memory\n");
        return MX_ERR_IO_DATA_INTEGRITY;
    }

    // We already read a page, now we need the rest...
    // The rest is the length in the header, minus what we already read, but accounting for
    // the bootdata_kernel_t portion of magenta_kernel_t that's included in the header length.
    uintptr_t data_off = kKernelOffset + PAGE_SIZE;
    size_t data_len = header->hdr_kernel.length -
                      (PAGE_SIZE - sizeof(magenta_kernel_t) + sizeof(bootdata_kernel_t));

    ssize_t ret = read(fd, reinterpret_cast<void*>(addr + data_off), data_len);
    if (ret < 0 || (size_t)ret != data_len) {
        fprintf(stderr, "Failed to read Magenta kernel data\n");
        return MX_ERR_IO;
    }

    *guest_ip = header->data_kernel.entry64;
    *end_off = header->hdr_file.length + sizeof(bootdata_t);
    return MX_OK;
}

static mx_status_t load_cmdline(const char* cmdline, const uintptr_t addr,
                                const uintptr_t bootdata_off) {
    bootdata_t* bootdata_hdr = (bootdata_t*)(addr + bootdata_off);
    uintptr_t data_off = bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(bootdata_hdr->length);

    bootdata_t* cmdline_hdr = (bootdata_t*)(addr + data_off);
    cmdline_hdr->type = BOOTDATA_CMDLINE;
    size_t cmdline_len = strlen(cmdline) + 1;
    if (cmdline_len > UINT32_MAX) {
        fprintf(stderr, "Command line length is outside of 32-bit range\n");
        return MX_ERR_OUT_OF_RANGE;
    }
    cmdline_hdr->length = cmdline_len & UINT32_MAX;
    memcpy(cmdline_hdr + 1, cmdline, cmdline_len);

    bootdata_hdr->length += cmdline_hdr->length + static_cast<uint32_t>(sizeof(bootdata_t));
    return MX_OK;
}

static mx_status_t load_bootfs(const int fd, const uintptr_t addr, const uintptr_t bootdata_off) {
    bootdata_t ramdisk_hdr;
    ssize_t ret = read(fd, &ramdisk_hdr, sizeof(bootdata_t));
    if (ret != sizeof(bootdata_t)) {
        fprintf(stderr, "Failed to read BOOTFS image header\n");
        return MX_ERR_IO;
    }

    if (!container_is_valid(&ramdisk_hdr)) {
        fprintf(stderr, "Invalid BOOTFS container\n");
        return MX_ERR_IO_DATA_INTEGRITY;
    }

    bootdata_t* bootdata_hdr = (bootdata_t*)(addr + bootdata_off);
    uintptr_t data_off = bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(bootdata_hdr->length);
    ret = read(fd, (void*)(addr + data_off), ramdisk_hdr.length);
    if (ret < 0 || (size_t)ret != ramdisk_hdr.length) {
        fprintf(stderr, "Failed to read BOOTFS image data\n");
        return MX_ERR_IO;
    }

    bootdata_hdr->length += ramdisk_hdr.length + static_cast<uint32_t>(sizeof(bootdata_t));
    return MX_OK;
}

static mx_status_t create_bootdata(uintptr_t addr, size_t size, uintptr_t acpi_off,
                                   uintptr_t bootdata_off) {
    if (BOOTDATA_ALIGN(bootdata_off) != bootdata_off)
        return MX_ERR_INVALID_ARGS;

    const size_t e820_size = guest_e820_size(size);
    const size_t bootdata_len = sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(uint64_t)) +
                                sizeof(bootdata_t) + BOOTDATA_ALIGN(e820_size);
    if (bootdata_off + bootdata_len > size)
        return MX_ERR_BUFFER_TOO_SMALL;
    if (bootdata_len > UINT32_MAX)
        return MX_ERR_OUT_OF_RANGE;

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

static bool is_magenta(const uintptr_t first_page) {
    magenta_kernel_t* header = (magenta_kernel_t*)first_page;
    return container_is_valid(&header->hdr_file);
}

mx_status_t setup_magenta(const uintptr_t addr, const size_t size, const uintptr_t first_page,
                          const uintptr_t acpi_off, const int fd, const char* bootdata_path,
                          const char* cmdline, uintptr_t* guest_ip, uintptr_t* bootdata_offset) {
    if (!is_magenta(first_page)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    mx_status_t status = create_bootdata(addr, size, acpi_off, kBootdataOffset);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create bootdata\n");
        return status;
    }

    uintptr_t magenta_end_off;
    status = load_magenta(fd, addr, size, first_page, guest_ip, &magenta_end_off);
    if (status != MX_OK)
        return status;
    MX_ASSERT(magenta_end_off <= kBootdataOffset);

    // If we have a command line, load it.
    if (cmdline != NULL) {
        status = load_cmdline(cmdline, addr, kBootdataOffset);
        if (status != MX_OK)
            return status;
    }

    // If we have been provided a BOOTFS image, load it.
    if (bootdata_path) {
        int boot_fd = open(bootdata_path, O_RDONLY);
        if (boot_fd < 0) {
            fprintf(stderr, "Failed to open BOOTFS image \"%s\"\n", bootdata_path);
            return MX_ERR_IO;
        }

        status = load_bootfs(boot_fd, addr, kBootdataOffset);
        close(boot_fd);
        if (status != MX_OK)
            return status;
    }
    *bootdata_offset = kBootdataOffset;
    return MX_OK;
}
