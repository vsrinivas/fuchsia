// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <hypervisor/guest.h>
#include <magenta/assert.h>
#include <magenta/boot/bootdata.h>

#include "magenta.h"

static const size_t kVmoSize = 1u << 30;
static const uintptr_t kKernelOffset = 0x100000;
static const uintptr_t kBootdataOffset = 0x800000;

static bool container_is_valid(const bootdata_t* container) {
    return container->type == BOOTDATA_CONTAINER &&
           container->length > sizeof(bootdata_t) &&
           container->extra == BOOTDATA_MAGIC &&
           container->flags == 0;
}

static mx_status_t load_magenta(const int fd, uintptr_t addr, uintptr_t* guest_ip,
                                uintptr_t* end_off) {
    uintptr_t header_addr = addr + kKernelOffset;
    int ret = read(fd, (void*)header_addr, sizeof(magenta_kernel_t));
    if (ret != sizeof(magenta_kernel_t)) {
        fprintf(stderr, "Failed to read Magenta kernel header\n");
        return ERR_IO;
    }

    magenta_kernel_t* header = (magenta_kernel_t*)header_addr;
    if (!container_is_valid(&header->hdr_file)) {
        fprintf(stderr, "Invalid Magenta container\n");
        return ERR_IO_DATA_INTEGRITY;
    }
    if (header->hdr_kernel.type != BOOTDATA_KERNEL) {
        fprintf(stderr, "Invalid Magenta kernel header\n");
        return ERR_IO_DATA_INTEGRITY;
    }
    if (header->data_kernel.entry64 >= kVmoSize) {
        fprintf(stderr, "Kernel entry point is outside of guest physical memory\n");
        return ERR_IO_DATA_INTEGRITY;
    }

    uintptr_t data_off = kKernelOffset + sizeof(magenta_kernel_t);
    uintptr_t data_addr = addr + data_off;
    size_t data_len = header->hdr_kernel.length - sizeof(bootdata_kernel_t);
    ret = read(fd, (void*)data_addr, data_len);
    if (ret < 0 || (size_t)ret != data_len) {
        fprintf(stderr, "Failed to read Magenta kernel data\n");
        return ERR_IO;
    }

    *guest_ip = header->data_kernel.entry64;
    *end_off = header->hdr_file.length + sizeof(bootdata_t);
    return NO_ERROR;
}

static mx_status_t load_bootfs(const int fd, uintptr_t addr, uintptr_t bootdata_off) {
    bootdata_t ramdisk_hdr;
    int ret = read(fd, &ramdisk_hdr, sizeof(bootdata_t));
    if (ret != sizeof(bootdata_t)) {
        fprintf(stderr, "Failed to read BOOTFS image header\n");
        return ERR_IO;
    }

    if (!container_is_valid(&ramdisk_hdr)) {
        fprintf(stderr, "Invalid BOOTFS container\n");
        return ERR_IO_DATA_INTEGRITY;
    }

    bootdata_t* bootdata_hdr = (bootdata_t*)(addr + bootdata_off);
    uintptr_t data_off = bootdata_off + sizeof(bootdata_t) + BOOTDATA_ALIGN(bootdata_hdr->length);
    uintptr_t data_addr = addr + data_off;
    ret = read(fd, (void*)data_addr, ramdisk_hdr.length);
    if (ret < 0 || (size_t)ret != ramdisk_hdr.length) {
        fprintf(stderr, "Failed to read BOOTFS image data\n");
        return ERR_IO;
    }

    bootdata_hdr->length += ramdisk_hdr.length;
    return NO_ERROR;
}

mx_status_t setup_magenta(const uintptr_t addr, const uintptr_t acpi_off,
                          const int fd, const char* bootdata_path, uintptr_t* guest_ip,
                          uintptr_t* bootdata_offset) {

    mx_status_t status = guest_create_bootdata(addr, kVmoSize, acpi_off, kBootdataOffset);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create bootdata\n");
        return status;
    }

    uintptr_t magenta_end_off;
    status = load_magenta(fd, addr, guest_ip, &magenta_end_off);
    if (status != NO_ERROR)
        return status;
    MX_ASSERT(magenta_end_off <= kBootdataOffset);

    // If we have been provided a BOOTFS image, load it.
    if (bootdata_path) {
        int boot_fd = open(bootdata_path, O_RDONLY);
        if (boot_fd < 0) {
            fprintf(stderr, "Failed to open BOOTFS image image \"%s\"\n", bootdata_path);
            return ERR_IO;
        }

        status = load_bootfs(boot_fd, addr, kBootdataOffset);
        close(boot_fd);
        if (status != NO_ERROR)
            return status;
    }
    *bootdata_offset = kBootdataOffset;
    return NO_ERROR;
}
