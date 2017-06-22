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
    // Move the first page to where magenta would like it to be
    uintptr_t header_addr = (uintptr_t) memmove((void*)(addr + kKernelOffset),
                                                (void*)first_page,
                                                PAGE_SIZE);

    magenta_kernel_t* header = (magenta_kernel_t*)header_addr;
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
                      (PAGE_SIZE - (sizeof(magenta_kernel_t) - sizeof(bootdata_kernel_t)));

    int ret = read(fd, (void*)(addr + data_off), data_len);
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
    cmdline_hdr->length = cmdline_len;
    memcpy(cmdline_hdr + 1, cmdline, cmdline_len);

    bootdata_hdr->length += cmdline_hdr->length + sizeof(bootdata_t);
    return MX_OK;
}

static mx_status_t load_bootfs(const int fd, const uintptr_t addr, const uintptr_t bootdata_off) {
    bootdata_t ramdisk_hdr;
    int ret = read(fd, &ramdisk_hdr, sizeof(bootdata_t));
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

    bootdata_hdr->length += ramdisk_hdr.length + sizeof(bootdata_t);
    return MX_OK;
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

    mx_status_t status = guest_create_bootdata(addr, size, acpi_off, kBootdataOffset);
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
            fprintf(stderr, "Failed to open BOOTFS image image \"%s\"\n", bootdata_path);
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
