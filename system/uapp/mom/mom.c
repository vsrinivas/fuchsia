// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hypervisor/guest.h>
#include <magenta/boot/bootdata.h>
#include <magenta/process.h>
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

static mx_status_t load_kernel_image(void* addr, FILE* file, uintptr_t* guest_entry) {
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    size_t header_size = sizeof(magenta_kernel_t);

    if (file_size < header_size) {
        printf("File is too small to be a magenta image.\n");
        return ERR_BAD_STATE;
    }

    if (file_size > kVmoSize - kKernelLoadOffset) {
        printf("File is too large to be loaded into the vmo.\n");
        return ERR_BAD_STATE;
    }

    rewind(file);
    if (file_size != fread(addr, 1, file_size, file)) {
        printf("Couldn't copy %d byte image. That's a shame.\n", (int) file_size);
        return ERR_IO;
    }

    magenta_kernel_t* header = (magenta_kernel_t*) addr;
    if (header->hdr_kernel.type != BOOTDATA_KERNEL) {
        printf("Invalid kernel header type.\n");
        return ERR_BAD_STATE;
    }

    if (header->data_kernel.entry64 > kVmoSize) {
        printf("Kernel entry point is outside of addressable space.\n");
        return ERR_BAD_STATE;
    }

    if (file_size != (header_size - sizeof(bootdata_kernel_t) + header->hdr_kernel.length)) {
        printf("File size does not match payload size in header.\n");
        return ERR_BAD_STATE;
    }

    *guest_entry = header->data_kernel.entry64;
    return NO_ERROR;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("usage: mom <path to magenta.bin>\n");
        return ERR_INVALID_ARGS;
    }

    mx_status_t status;
    mx_handle_t hypervisor;
    status = mx_hypervisor_create(MX_HANDLE_INVALID, 0, &hypervisor);
    if (status != NO_ERROR) {
        printf("Failed to create hypervisor\n");
        return status;
    }

    uintptr_t addr;
    mx_handle_t guest_phys_mem;
    status = guest_create_phys_mem(&addr, kVmoSize, &guest_phys_mem);
    if (status != NO_ERROR) {
        printf("Failed to create guest physical memory\n");
        return status;
    }

    mx_handle_t guest_serial_fifo;
    mx_handle_t guest;
    status = guest_create(hypervisor, guest_phys_mem, &guest_serial_fifo, &guest);
    if (status != NO_ERROR) {
        printf("Failed to create guest\n");
        return status;
    }

    uintptr_t guest_entry = 0;
    status = guest_create_identity_pt(addr, kVmoSize, &guest_entry);
    if (status != NO_ERROR) {
        printf("Failed to create page table\n");
        return status;
    }

    printf("Loading %s\n", argv[1]);
    FILE* magenta_file = fopen(argv[1], "rb");
    if (magenta_file == NULL) {
        printf("%s not found\n", argv[1]);
        return status;
    }

    status = load_kernel_image((void*)(addr + kKernelLoadOffset), magenta_file, &guest_entry);
    if (status != NO_ERROR) {
        printf("Failed to load kernel into VMO\n");
        return status;
    }
    fclose(magenta_file);

#if __x86_64__
    uintptr_t guest_cr3 = 0;
    status = mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_CR3, &guest_cr3,
                              sizeof(guest_cr3), NULL, 0);
    if (status != NO_ERROR) {
        printf("Failed to set guest CR3\n");
        return status;
    }
#endif // __x86_64__

    status = mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY,
                              &guest_entry, sizeof(guest_entry), NULL, 0);
    if (status != NO_ERROR) {
        printf("Failed to set guest RIP\n");
        return status;
    }

    status = mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_ENTER, NULL, 0, NULL, 0);
    if (status != NO_ERROR) {
        printf("Failed to enter guest\n");
        return status;
    }

    return NO_ERROR;
}
