// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/acpi.h>
#include <hypervisor/block.h>
#include <hypervisor/guest.h>
#include <hypervisor/uart.h>
#include <hypervisor/vcpu.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

#include "linux.h"
#include "magenta.h"

static const uint64_t kVmoSize = 1u << 30;
static const uint16_t kPioEnable = 1u << 0;
static const uintptr_t kPioBase = 0x8000;
static const uint32_t kMapFlags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;

static mx_status_t usage(const char* cmd) {
    fprintf(stderr, "usage: %s [-b block.bin] kernel.bin [ramdisk.bin]\n", cmd);
    return MX_ERR_INVALID_ARGS;
}

static mx_status_t create_vmo(uint64_t size, uintptr_t* addr, mx_handle_t* vmo) {
    mx_status_t status = mx_vmo_create(size, 0, vmo);
    if (status != MX_OK)
        return status;
    return mx_vmar_map(mx_vmar_root_self(), 0, *vmo, 0, size, kMapFlags, addr);
}

int main(int argc, char** argv) {
    const char* cmd = basename(argv[0]);
    const char* block_path = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "b:")) != -1) {
        switch (opt) {
        case 'b':
            block_path = optarg;
            break;
        default:
            return usage(cmd);
        }
    }
    if (optind >= argc)
        return usage(cmd);
    argc -= optind;
    argv += optind;

    uintptr_t addr;
    mx_handle_t physmem_vmo;
    mx_status_t status = create_vmo(kVmoSize, &addr, &physmem_vmo);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create guest physical memory\n");
        return status;
    }

    mx_handle_t resource;
    status = guest_get_resource(&resource);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to get resource\n");
        return status;
    }

    mx_handle_t guest;
    status = mx_guest_create(resource, 0, physmem_vmo, &guest);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create guest\n");
        return status;
    }
    mx_handle_close(resource);

    guest_state_t guest_state;
    memset(&guest_state, 0, sizeof(guest_state));
    guest_state.guest = guest;
    int ret = mtx_init(&guest_state.mutex, mtx_plain);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to initialize guest state mutex\n");
        return MX_ERR_INTERNAL;
    }
    // Setup guest memory.
    guest_state.mem_addr = (void*)addr;
    guest_state.mem_size = kVmoSize;
    // Setup UART.
    uart_state_t uart_state;
    guest_state.uart_state = &uart_state;
    status = uart_init(&uart_state);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to initialize UART state\n");
        return status;
    }
    // Setup block device.
    block_state_t block_state;
    guest_state.block_state = &block_state;
    if (block_path != NULL) {
        status = block_init(&block_state, block_path);
        if (status != MX_OK)
            return status;
    } else {
        block_state.fd = -1;
    }
    // Setup each PCI device's BAR 0 register.
    for (unsigned i = 0; i < PCI_MAX_DEVICES; i++) {
        pci_device_state_t* pci_device_state = &guest_state.pci_device_state[i];
        pci_device_state->command = kPioEnable;
        pci_device_state->bar[0] = kPioBase + (i << 8);
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
    int fd = open(argv[0], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open kernel image \"%s\"\n", argv[0]);
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
    const char* ramdisk_path = argc >= 2 ? argv[1] : NULL;
    status = setup_magenta(addr, kVmoSize, first_page, pt_end_off, fd, ramdisk_path, NULL,
                           &guest_ip, &bootdata_off);
    if (status == MX_ERR_NOT_SUPPORTED) {
        char cmdline[UINT8_MAX];
        const char* fmt_string = "earlyprintk=serial,ttyS,115200 acpi_rsdp=%#" PRIx64
                                 " io_delay=none console=ttyS0";
        snprintf(cmdline, UINT8_MAX, fmt_string, pt_end_off);
        status = setup_linux(
                addr, kVmoSize, first_page, fd, ramdisk_path, cmdline, &guest_ip, &bootdata_off);
    }
    if (status == MX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Unknown kernel\n");
        return status;
    } else if (status != MX_OK) {
        fprintf(stderr, "Failed to load kernel\n");
        return status;
    }
    close(fd);

#if __x86_64__
    uintptr_t apic_addr;
    mx_handle_t apic_vmo;
    status = create_vmo(PAGE_SIZE, &apic_addr, &apic_vmo);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create VCPU local APIC memory\n");
        return status;
    }
#endif // __x86_64__

    mx_vcpu_create_args_t args = {
        guest_ip,
#if __x86_64__
        0 /* cr3 */, apic_vmo,
#endif // __x86_64__
    };
    mx_handle_t vcpu;
    status = mx_vcpu_create(guest, 0, &args, &vcpu);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create VCPU\n");
        return status;
    }

    vcpu_context_t vcpu_context;
    vcpu_init(&vcpu_context);
    vcpu_context.vcpu = vcpu;
#if __x86_64__
    vcpu_context.local_apic_state.apic_addr = (void*)apic_addr;
#endif // __x86_64__
    vcpu_context.guest_state = &guest_state;

    mx_vcpu_state_t vcpu_state;
    memset(&vcpu_state, 0, sizeof(vcpu_state));
#if __x86_64__
    vcpu_state.rsi = bootdata_off;
#endif // __x86_64__
    status = mx_vcpu_write_state(vcpu, MX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    if (status != MX_OK) {
        fprintf(stderr, "Failed to write VCPU state\n");
        return status;
    }

    status = vcpu_loop(&vcpu_context);
    if (status != MX_OK)
        fprintf(stderr, "Failed to enter guest %d\n", status);
    return status;
}
