// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/acpi.h>
#include <hypervisor/guest.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

#include "magenta.h"
#include "linux.h"
#include "vcpu.h"

static const size_t kVmoSize = 1u << 30;
static const uint16_t kPioEnable = 1u << 0;
static const uintptr_t kPioBase = 0x8000;
static const uint32_t kMapFlags __UNUSED = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;

static int vcpu_thread(void* arg) {
    // TODO(abdulla): Correctly terminate the VCPU prior to return.
    return vcpu_loop((vcpu_context_t*)arg) != MX_OK ? thrd_error : thrd_success;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s kernel.bin [ramdisk.bin]\n", basename(argv[0]));
        return MX_ERR_INVALID_ARGS;
    }

    mx_handle_t hypervisor;
    mx_status_t status = mx_hypervisor_create(MX_HANDLE_INVALID, 0, &hypervisor);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create hypervisor\n");
        return status;
    }

    uintptr_t addr;
    mx_handle_t phys_mem;
    status = guest_create_phys_mem(&addr, kVmoSize, &phys_mem);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create guest physical memory\n");
        return status;
    }

    guest_state_t guest_state;
    memset(&guest_state, 0, sizeof(guest_state));
    int ret = mtx_init(&guest_state.mutex, mtx_plain);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to initialize guest state mutex\n");
        return MX_ERR_INTERNAL;
    }
    // Setup guest memory.
    guest_state.mem_addr = (void*)addr;
    guest_state.mem_size = kVmoSize;
    // Setup each PCI device's BAR 0 register.
    for (unsigned i = 0; i < PCI_MAX_DEVICES; i++) {
        pci_device_state_t* pci_device_state = &guest_state.pci_device_state[i];
        pci_device_state->command = kPioEnable;
        pci_device_state->bar[0] = kPioBase + (i << 8);
    }

    vcpu_context_t context;
    memset(&context, 0, sizeof(context));
    context.guest_state = &guest_state;

    status = guest_create(hypervisor, phys_mem, &context.vcpu_fifo, &context.guest);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create guest\n");
        return status;
    }

    uintptr_t pt_end_off;
    status = guest_create_page_table(addr, kVmoSize, &pt_end_off);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create page table\n");
        return status;
    }

    status = guest_create_acpi_table(addr, kVmoSize, pt_end_off);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create ACPI table\n");
        return status;
    }

    // Prepare the OS image
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open kernel image \"%s\"\n", argv[1]);
        return MX_ERR_IO;
    }

    // Load the first page in to allow OS detection without requiring
    // us to seek backwards later.
    uintptr_t first_page = addr + kVmoSize - PAGE_SIZE;
    ret = read(fd, (void*)first_page, PAGE_SIZE);
    if (ret != PAGE_SIZE) {
        fprintf(stderr, "Failed to read first page of kernel\n");
        return MX_ERR_IO;
    }

    uintptr_t guest_ip;
    uintptr_t bootdata_off = 0;
    const char* ramdisk_path = argc >= 3 ? argv[2] : NULL;
    status = setup_magenta(addr, kVmoSize, first_page, pt_end_off, fd, ramdisk_path, NULL,
                           &guest_ip, &bootdata_off);
    if (status == MX_ERR_NOT_SUPPORTED) {
        char cmdline[UINT8_MAX];
        snprintf(cmdline, UINT8_MAX,
                 "earlyprintk=serial,ttyS,115200 pci=noearly acpi_rsdp=%#" PRIx64, pt_end_off);
        status = setup_linux(addr, kVmoSize, first_page, fd, cmdline, &guest_ip, &bootdata_off);
    }
    if (status == MX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Unknown kernel\n");
        return status;
    } else if (status != MX_OK) {
        fprintf(stderr, "Failed to load kernel\n");
        return status;
    }
    close(fd);

    mx_guest_gpr_t guest_gpr;
    memset(&guest_gpr, 0, sizeof(guest_gpr));
#if __x86_64__
    guest_gpr.rsi = bootdata_off;
#endif // __x86_64__
    status = mx_hypervisor_op(context.guest, MX_HYPERVISOR_OP_GUEST_SET_GPR,
                              &guest_gpr, sizeof(guest_gpr), NULL, 0);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to set guest ESI\n");
        return status;
    }

    status = mx_hypervisor_op(context.guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY_IP,
                              &guest_ip, sizeof(guest_ip), NULL, 0);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to set guest RIP\n");
        return status;
    }

#if __x86_64__
    uintptr_t guest_cr3 = 0;
    status = mx_hypervisor_op(context.guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY_CR3,
                              &guest_cr3, sizeof(guest_cr3), NULL, 0);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to set guest CR3\n");
        return status;
    }

    status = mx_vmo_create(PAGE_SIZE, 0, &context.local_apic_state.apic_mem);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create guest local APIC memory\n");
        return status;
    }

    status = mx_hypervisor_op(context.guest, MX_HYPERVISOR_OP_GUEST_SET_APIC_MEM,
                              &context.local_apic_state.apic_mem, sizeof(mx_handle_t), NULL, 0);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to set guest local APIC memory\n");
        return status;
    }

    status = mx_vmar_map(mx_vmar_root_self(), 0, context.local_apic_state.apic_mem, 0, PAGE_SIZE,
                         kMapFlags, (uintptr_t*)&context.local_apic_state.apic_addr);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to map local APIC memory\n");
        return status;
    }
#endif // __x86_64__

    thrd_t thread;
    ret = thrd_create(&thread, vcpu_thread, &context);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create control thread\n");
        return MX_ERR_INTERNAL;
    }
    ret = thrd_detach(thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach control thread\n");
        return MX_ERR_INTERNAL;
    }

    status = mx_hypervisor_op(context.guest, MX_HYPERVISOR_OP_GUEST_ENTER, NULL, 0, NULL, 0);
    if (status != MX_OK)
        fprintf(stderr, "Failed to enter guest %d\n", status);
    return status;
}
