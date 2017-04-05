// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/boot/bootdata.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

static const uint32_t kMapFlags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
static const uint64_t kVmoSize = 1 << 30;

#if __x86_64__
#define KERNEL_LOAD_OFFSET 0x100000

#define X86_MMU_PG_P       0x0001    /* P    Valid           */
#define X86_MMU_PG_RW      0x0002    /* R/W  Read/Write      */
#define X86_MMU_PG_U       0x0004    /* U/S  User/Supervisor */
#define X86_MMU_PG_PS      0x0080    /* PS   Page size       */

static void guest_setup(uint8_t* addr) {
    memset(addr, 0, kVmoSize);

    // Setup a single 1Gb page.
    uint64_t* pml4 = (uint64_t*) addr;
    uint64_t* pdp = (uint64_t*) (addr + PAGE_SIZE);

    *pml4 = PAGE_SIZE | X86_MMU_PG_P | X86_MMU_PG_RW | X86_MMU_PG_U;
    *pdp = X86_MMU_PG_P | X86_MMU_PG_RW | X86_MMU_PG_U | X86_MMU_PG_PS;
}

// Header blob for magenta.bin
typedef struct {
    bootdata_t hdr_file;
    bootdata_t hdr_kernel;
    bootdata_kernel_t data_kernel;
} magenta_kernel_t;

static mx_status_t load_kernel_image(uint8_t* addr, FILE* file, uintptr_t* guest_entry) {
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    size_t header_size = sizeof(magenta_kernel_t);

    if (file_size < header_size) {
        printf("File is too small to be a magenta image.\n");
        return ERR_BAD_STATE;
    }

    if (file_size > kVmoSize - KERNEL_LOAD_OFFSET) {
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
#endif // __x86_64__

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("usage: mom <path to magenta.bin>\n");
        return ERR_INVALID_ARGS;
    }

    mx_status_t status;
    mx_handle_t hypervisor;
    status = mx_hypervisor_create(MX_HANDLE_INVALID, 0, &hypervisor);
    if (status != NO_ERROR) {
        printf("Unable to create hypervisor to launch Magenta on Magenta.\n");
        return status;
    }

    mx_handle_t vmo;
    status = mx_vmo_create(kVmoSize, 0, &vmo);
    if (status != NO_ERROR) {
        printf("Failed to create vmo of size %zu\n", kVmoSize);
        return status;
    }

    uintptr_t mapped_addr;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, kVmoSize, kMapFlags, &mapped_addr);
    if (status != NO_ERROR) {
        printf("Failed to map vmo.\n");
        return status;
    }

    mx_handle_t guest;
    status = mx_hypervisor_op(hypervisor, MX_HYPERVISOR_OP_GUEST_CREATE,
                              &vmo, sizeof(vmo), &guest, sizeof(guest));
    if (status != NO_ERROR) {
        printf("Failed to create guest.\n");
        return status;
    }

    uintptr_t guest_entry = 0;
#if __x86_64__
    guest_setup((uint8_t*) mapped_addr);

    printf("Loading %s\n", argv[1]);
    FILE* magenta_file = fopen(argv[1], "rb");
    if (magenta_file == NULL) {
        printf("%s not found.\n", argv[1]);
        return ERR_BAD_PATH;
    }
    status = load_kernel_image(
            (uint8_t*) (mapped_addr + KERNEL_LOAD_OFFSET), magenta_file, &guest_entry);
    if (status != NO_ERROR) {
        printf("Failed to load magenta into vmo.\n");
        fclose(magenta_file);
        return status;
    }
    fclose(magenta_file);

    uintptr_t guest_cr3 = 0;
    status = mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_CR3, &guest_cr3,
                              sizeof(guest_cr3), NULL, 0);
    if (status != NO_ERROR) {
        printf("Error setting guest CR3.\n");
        return status;
    }
#endif // __x86_64__
    status = mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY,
                              &guest_entry, sizeof(guest_entry), NULL, 0);
    if (status != NO_ERROR) {
        printf("Error setting guest RIP.\n");
        return status;
    }
    status = mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_ENTER, NULL, 0, NULL, 0);
    if (status != NO_ERROR) {
        printf("Error entering guest.\n");
        return status;
    }

    mx_handle_close(guest);
    mx_handle_close(vmo);
    mx_handle_close(hypervisor);

    return EXIT_SUCCESS;
}
