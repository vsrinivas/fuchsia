// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/guest.h>
#include <magenta/boot/bootdata.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

static const uint64_t kVmoSize = 1 << 30;
static const uintptr_t kKernelLoadOffset = 0x100000;

// Header blob for magenta.bin
typedef struct {
    bootdata_t hdr_file;
    bootdata_t hdr_kernel;
    bootdata_kernel_t data_kernel;
} magenta_kernel_t;

static mx_status_t load_magenta(int fd, uintptr_t addr, uintptr_t* guest_entry) {
    uintptr_t header_addr = addr + kKernelLoadOffset;
    int rc = read(fd, (void*)header_addr, sizeof(magenta_kernel_t));
    if (rc != sizeof(magenta_kernel_t)) {
        fprintf(stderr, "Failed to read Magenta kernel header\n");
        return ERR_IO;
    }

    magenta_kernel_t* header = (magenta_kernel_t*)header_addr;
    if (header->hdr_kernel.type != BOOTDATA_KERNEL) {
        fprintf(stderr, "Invalid Magenta kernel header\n");
        return ERR_IO_DATA_INTEGRITY;
    }
    if (header->data_kernel.entry64 >= kVmoSize) {
        fprintf(stderr, "Kernel entry point is outside of guest physical memory\n");
        return ERR_IO_DATA_INTEGRITY;
    }

    uintptr_t data_addr = header_addr + sizeof(magenta_kernel_t);
    rc = read(fd, (void*)data_addr, kVmoSize - data_addr);
    if (rc < 0 || (uint64_t)rc < header->hdr_kernel.length - sizeof(bootdata_kernel_t)) {
        fprintf(stderr, "Failed to read Magenta kernel data\n");
        return ERR_IO;
    }

    *guest_entry = header->data_kernel.entry64;
    return NO_ERROR;
}

static mx_status_t read_serial_fifo(mx_handle_t fifo) {
    static uint8_t buffer[PAGE_SIZE];
    static uint32_t bytes_read;
    static uint32_t offset = 0;

    mx_status_t status = mx_fifo_read(fifo, buffer + offset, PAGE_SIZE - offset, &bytes_read);
    if (status != NO_ERROR)
        return status;

    uint8_t* linebreak = memchr(buffer + offset, '\r', bytes_read);
    offset += bytes_read;
    if (linebreak != NULL || offset == PAGE_SIZE) {
        printf("%.*s", offset, buffer);
        offset = 0;
    }
    return NO_ERROR;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <path to kernel.bin>\n", basename(argv[0]));
        return ERR_INVALID_ARGS;
    }

    mx_status_t status;
    mx_handle_t hypervisor;
    status = mx_hypervisor_create(MX_HANDLE_INVALID, 0, &hypervisor);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create hypervisor\n");
        return status;
    }

    uintptr_t addr;
    mx_handle_t guest_phys_mem;
    status = guest_create_phys_mem(&addr, kVmoSize, &guest_phys_mem);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create guest physical memory\n");
        return status;
    }

    mx_handle_t guest_serial_fifo;
    mx_handle_t guest;
    status = guest_create(hypervisor, guest_phys_mem, &guest_serial_fifo, &guest);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create guest\n");
        return status;
    }

    uintptr_t pte_off;
    status = guest_create_page_table(addr, kVmoSize, &pte_off);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create page table\n");
        return status;
    }

    status = guest_create_acpi_table(addr, kVmoSize, pte_off);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create ACPI table\n");
        return status;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open Magenta kernel image \"%s\"\n", argv[1]);
        return ERR_IO;
    }

    uintptr_t guest_entry;
    status = load_magenta(fd, addr, &guest_entry);
    close(fd);
    if (status != NO_ERROR)
        return status;

#if __x86_64__
    uintptr_t guest_cr3 = 0;
    status = mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_CR3, &guest_cr3,
                              sizeof(guest_cr3), NULL, 0);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to set guest CR3\n");
        return status;
    }
#endif // __x86_64__

    status = mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY,
                              &guest_entry, sizeof(guest_entry), NULL, 0);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to set guest RIP\n");
        return status;
    }

    do {
        status = mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_ENTER, NULL, 0, NULL, 0);
        read_serial_fifo(guest_serial_fifo);
    } while(status == NO_ERROR);
    fprintf(stderr, "Failed to enter guest %d\n", status);
    return status;
}
