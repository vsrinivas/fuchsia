// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/decode.h>
#include <hypervisor/guest.h>
#include <magenta/assert.h>
#include <magenta/boot/bootdata.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

#include "vcpu.h"

static const size_t kVmoSize = 1u << 30;
static const uintptr_t kKernelOffset = 0x100000;
static const uintptr_t kBootdataOffset = 0x800000;

static const uint32_t kMapFlags __UNUSED = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;

static bool container_is_valid(const bootdata_t* container) {
    return container->type == BOOTDATA_CONTAINER &&
           container->length > sizeof(bootdata_t) &&
           container->extra == BOOTDATA_MAGIC &&
           container->flags == 0;
}

static mx_status_t load_magenta(int fd, uintptr_t addr, uintptr_t* guest_ip,
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

static mx_status_t load_bootfs(int fd, uintptr_t addr, uintptr_t bootdata_off) {
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

static int vcpu_thread(void* arg) {
    // TODO(abdulla): Correctly terminate the VCPU prior to return.
    return vcpu_loop((vcpu_context_t*)arg) != NO_ERROR ? thrd_error : thrd_success;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s kernel.bin [ramdisk.bin]\n", basename(argv[0]));
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
    mx_handle_t phys_mem;
    status = guest_create_phys_mem(&addr, kVmoSize, &phys_mem);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create guest physical memory\n");
        return status;
    }

    guest_state_t guest_state;
    memset(&guest_state, 0, sizeof(guest_state));
    int ret = mtx_init(&guest_state.mutex, mtx_plain);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to initialize guest state mutex\n");
        return ERR_INTERNAL;
    }

    vcpu_context_t context;
    memset(&context, 0, sizeof(context));
    context.guest_state = &guest_state;

    status = guest_create(hypervisor, phys_mem, &context.vcpu_fifo, &context.guest);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create guest\n");
        return status;
    }

    uintptr_t pt_end_off;
    status = guest_create_page_table(addr, kVmoSize, &pt_end_off);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create page table\n");
        return status;
    }

    status = guest_create_acpi_table(addr, kVmoSize, pt_end_off);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create ACPI table\n");
        return status;
    }

    status = guest_create_bootdata(addr, kVmoSize, pt_end_off, kBootdataOffset);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create bootdata\n");
        return status;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open Magenta kernel image \"%s\"\n", argv[1]);
        return ERR_IO;
    }

    uintptr_t guest_ip;
    uintptr_t magenta_end_off;
    status = load_magenta(fd, addr, &guest_ip, &magenta_end_off);
    close(fd);
    if (status != NO_ERROR)
        return status;
    MX_ASSERT(magenta_end_off <= kBootdataOffset);

    // If we have been provided a BOOTFS image, load it.
    if (argc >= 3) {
        fd = open(argv[2], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Failed to open BOOTFS image image \"%s\"\n", argv[2]);
            return ERR_IO;
        }

        status = load_bootfs(fd, addr, kBootdataOffset);
        close(fd);
        if (status != NO_ERROR)
            return status;
    }

    mx_guest_gpr_t guest_gpr;
    memset(&guest_gpr, 0, sizeof(guest_gpr));
#if __x86_64__
    guest_gpr.rsi = kBootdataOffset;
#endif // __x86_64__
    status = mx_hypervisor_op(context.guest, MX_HYPERVISOR_OP_GUEST_SET_GPR,
                              &guest_gpr, sizeof(guest_gpr), NULL, 0);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to set guest ESI\n");
        return status;
    }

    status = mx_hypervisor_op(context.guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY_IP,
                              &guest_ip, sizeof(guest_ip), NULL, 0);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to set guest RIP\n");
        return status;
    }

#if __x86_64__
    uintptr_t guest_cr3 = 0;
    status = mx_hypervisor_op(context.guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY_CR3,
                              &guest_cr3, sizeof(guest_cr3), NULL, 0);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to set guest CR3\n");
        return status;
    }

    status = mx_vmo_create(PAGE_SIZE, 0, &context.local_apic_state.apic_mem);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to create guest local APIC memory\n");
        return status;
    }

    status = mx_hypervisor_op(context.guest, MX_HYPERVISOR_OP_GUEST_SET_APIC_MEM,
                              &context.local_apic_state.apic_mem, sizeof(mx_handle_t), NULL, 0);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to set guest local APIC memory\n");
        return status;
    }

    status = mx_vmar_map(mx_vmar_root_self(), 0, context.local_apic_state.apic_mem, 0, PAGE_SIZE,
                         kMapFlags, (uintptr_t*)&context.local_apic_state.apic_addr);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to map local APIC memory\n");
        return status;
    }
#endif // __x86_64__

    thrd_t thread;
    ret = thrd_create(&thread, vcpu_thread, &context);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create control thread\n");
        return ERR_INTERNAL;
    }
    ret = thrd_detach(thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach control thread\n");
        return ERR_INTERNAL;
    }

    status = mx_hypervisor_op(context.guest, MX_HYPERVISOR_OP_GUEST_ENTER, NULL, 0, NULL, 0);
    if (status != NO_ERROR)
        fprintf(stderr, "Failed to enter guest %d\n", status);
    return status;
}
