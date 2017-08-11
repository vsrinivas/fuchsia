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
#include <hypervisor/io_apic.h>
#include <hypervisor/io_port.h>
#include <hypervisor/pci.h>
#include <hypervisor/uart.h>
#include <hypervisor/vcpu.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

#include "linux.h"
#include "magenta.h"

static const uint64_t kVmoSize = 1u << 30;
static const uint32_t kMapFlags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;

static mx_status_t usage(const char* cmd) {
    fprintf(stderr, "usage: %s [-b block.bin] [-r ramdisk.bin] [-c cmdline] kernel.bin\n", cmd);
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
    const char* ramdisk_path = NULL;
    const char* cmdline = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "b:r:c:")) != -1) {
        switch (opt) {
        case 'b':
            block_path = optarg;
            break;
        case 'r':
            ramdisk_path = optarg;
            break;
        case 'c':
            cmdline = optarg;
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
    ssize_t ret = read(fd, (void*)first_page, PAGE_SIZE);
    if (ret != PAGE_SIZE) {
        fprintf(stderr, "Failed to read first page of kernel\n");
        return MX_ERR_IO;
    }

    uintptr_t guest_ip;
    uintptr_t bootdata_off = 0;
    status = setup_magenta(addr, kVmoSize, first_page, pt_end_off, fd, ramdisk_path,
                           cmdline, &guest_ip, &bootdata_off);
    if (status == MX_ERR_NOT_SUPPORTED) {
        char linux_cmdline[PATH_MAX];
        const char* fmt_string = "earlyprintk=serial,ttyS,115200 acpi_rsdp=%#" PRIx64
                                 " io_delay=none console=ttyS0 %s";
        snprintf(linux_cmdline, PATH_MAX, fmt_string, pt_end_off, cmdline ? cmdline : "");
        status = setup_linux(addr, kVmoSize, first_page, fd, ramdisk_path, linux_cmdline, &guest_ip,
                             &bootdata_off);
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

    guest_ctx_t guest_ctx;
    memset(&guest_ctx, 0, sizeof(guest_ctx));
    // Setup IO APIC.
    io_apic_t io_apic;
    guest_ctx.io_apic = &io_apic;
    io_apic_init(&io_apic);
    // Setup IO ports.
    io_port_t io_port;
    guest_ctx.io_port = &io_port;
    io_port_init(&io_port);
    // Setup PCI.
    pci_bus_t bus;
    guest_ctx.bus = &bus;
    status = pci_bus_init(&bus);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create PCI bus.\n");
        return status;
    }
    // Setup UART.
    uart_t uart;
    guest_ctx.uart = &uart;
    uart_init(&uart, &io_apic);
    status = uart_async(&uart, vcpu, guest);
    if (status != MX_OK)
        return status;
    // Setup block device.
    block_t block;
    guest_ctx.block = &block;
    pci_device_t* virtio_block = &block.virtio_device.pci_device;
    if (block_path != NULL) {
        status = block_init(&block, block_path, (void*)addr, kVmoSize, &io_apic);
        if (status != MX_OK)
            return status;

        status = pci_bus_connect(&bus, virtio_block, PCI_DEVICE_VIRTIO_BLOCK);
        if (status != MX_OK)
            return status;

        status = block_async(&block, vcpu, guest, pci_bar_base(virtio_block),
                             pci_bar_size(virtio_block));
        if (status != MX_OK)
            return status;
    }

    vcpu_ctx_t vcpu_ctx;
    vcpu_init(&vcpu_ctx);
    vcpu_ctx.vcpu = vcpu;
#if __x86_64__
    vcpu_ctx.local_apic.apic_addr = (void*)apic_addr;
#endif // __x86_64__
    vcpu_ctx.guest_ctx = &guest_ctx;

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

    return vcpu_loop(&vcpu_ctx);
}
