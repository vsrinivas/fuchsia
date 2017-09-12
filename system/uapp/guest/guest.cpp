// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/acpi.h>
#include <hypervisor/balloon.h>
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
#include <fbl/unique_ptr.h>
#include <virtio/balloon.h>

#include "linux.h"
#include "magenta.h"

static const uint64_t kVmoSize = 1u << 30;
static const uint32_t kMapFlags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;

/* Unused memory above this threshold may be reclaimed by the balloon. */
static uint32_t balloon_threshold_pages = 1024;

static mx_status_t usage(const char* cmd) {
    fprintf(stderr, "usage: %s [OPTIONS] kernel.bin\n", cmd);
    fprintf(stderr, "\n");
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "\t-b [block.bin]     Use file 'block.bin' as a virtio-block device.\n");
    fprintf(stderr, "\t-r [ramdisk.bin]   Use file 'ramdisk.bin' as a ramdisk.\n");
    fprintf(stderr, "\t-c [cmdline]       Use string 'cmdline' as the kernel command line.\n");
    fprintf(stderr, "\t-m [seconds]       Poll the virtio-balloon device every 'seconds' seconds\n"
                    "\t                   and adjust the balloon size based on the amount of\n"
                    "\t                   unused guest memory.\n");
    fprintf(stderr, "\t-p [pages]         Number of unused pages to allow the guest to\n"
                    "\t                   retain. Has no effect unless -m is also used.\n");
    fprintf(stderr, "\t-d                 Demand-page balloon deflate requests.\n");
    fprintf(stderr, "\n");
    return MX_ERR_INVALID_ARGS;
}

static mx_status_t create_vmo(uint64_t size, uintptr_t* addr, mx_handle_t* vmo) {
    mx_status_t status = mx_vmo_create(size, 0, vmo);
    if (status != MX_OK)
        return status;
    return mx_vmar_map(mx_vmar_root_self(), 0, *vmo, 0, size, kMapFlags, addr);
}

static void balloon_stats_handler(const virtio_balloon_stat_t* stats, size_t len, void* ctx) {
    balloon_t* balloon = static_cast<balloon_t*>(ctx);
    for (size_t i = 0; i < len; ++i) {
        if (stats[i].tag != VIRTIO_BALLOON_S_AVAIL)
            continue;

        mtx_lock(&balloon->mutex);
        uint32_t current_pages = balloon->config.num_pages;
        mtx_unlock(&balloon->mutex);

        uint32_t available_pages = static_cast<uint32_t>(stats[i].val / VIRTIO_BALLOON_PAGE_SIZE);
        uint32_t target_pages = current_pages + (available_pages - balloon_threshold_pages);
        if (current_pages == target_pages)
            return;

        printf("virtio-balloon: adjusting target pages %#x -> %#x.\n",
               current_pages, target_pages);
        mx_status_t status = balloon_update_num_pages(balloon, target_pages);
        if (status != MX_OK)
            fprintf(stderr, "Error %d updating balloon size.\n", status);
        return;
    }
}

typedef struct balloon_task_args {
    balloon_t* balloon;
    mx_duration_t interval;
} balloon_task_args_t;

static int balloon_stats_task(void* ctx) {
    fbl::unique_ptr<balloon_task_args_t> args(static_cast<balloon_task_args_t*>(ctx));
    while (true) {
        mx_nanosleep(mx_deadline_after(args->interval));
        balloon_request_stats(args->balloon, &balloon_stats_handler, args->balloon);
    }
    return MX_OK;
}

static mx_status_t poll_balloon_stats(balloon_t* balloon, mx_duration_t interval) {
    thrd_t thread;
    auto args = new balloon_task_args_t{balloon, interval};

    int ret = thrd_create(&thread, balloon_stats_task, args);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create balloon thread %d\n", ret);
        delete args;
        return MX_ERR_INTERNAL;
    }

    ret = thrd_detach(thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach balloon thread %d\n", ret);
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
}

int main(int argc, char** argv) {
    const char* cmd = basename(argv[0]);
    const char* block_path = NULL;
    const char* ramdisk_path = NULL;
    const char* cmdline = NULL;
    mx_duration_t balloon_poll_interval = 0;
    bool balloon_deflate_on_demand = false;
    int opt;
    while ((opt = getopt(argc, argv, "b:r:c:m:dp:")) != -1) {
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
        case 'm':
            balloon_poll_interval = MX_SEC(strtoul(optarg, nullptr, 10));
            if (balloon_poll_interval <= 0) {
                fprintf(stderr, "Invalid balloon interval %s. Must be an integer greater than 0\n",
                        optarg);
                return MX_ERR_INVALID_ARGS;
            }
            break;
        case 'd':
            balloon_deflate_on_demand = true;
            break;
        case 'p':
            balloon_threshold_pages = static_cast<uint32_t>(strtoul(optarg, nullptr, 10));
            if (balloon_threshold_pages <= 0) {
                fprintf(stderr, "Invalid balloon threshold %s. Must be an integer greater than 0\n",
                        optarg);
                return MX_ERR_INVALID_ARGS;
            }
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

    char guest_cmdline[PATH_MAX];
    const char* magenta_fmt_string = "TERM=uart %s";
    snprintf(guest_cmdline, PATH_MAX, magenta_fmt_string, cmdline ? cmdline : "");
    status = setup_magenta(addr, kVmoSize, first_page, pt_end_off, fd, ramdisk_path,
                           guest_cmdline, &guest_ip, &bootdata_off);

    if (status == MX_ERR_NOT_SUPPORTED) {
        const char* linux_fmt_string = "earlyprintk=serial,ttyS,115200 console=ttyS0,115200 "
                                       "io_delay=none acpi_rsdp=%#lx %s";
        snprintf(guest_cmdline, PATH_MAX, linux_fmt_string, pt_end_off, cmdline ? cmdline : "");
        status = setup_linux(addr, kVmoSize, first_page, fd, ramdisk_path, guest_cmdline, &guest_ip,
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
        0 /* cr3 */,
        apic_vmo,
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
    status = pci_bus_init(&bus, &io_apic);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create PCI bus.\n");
        return status;
    }
    // Setup UART.
    uart_t uart;
    guest_ctx.uart = &uart;
    uart_init(&uart, &io_apic);
    status = uart_async(&uart, guest);
    if (status != MX_OK)
        return status;

    // Setup block device.
    block_t block;
    pci_device_t* virtio_block = &block.virtio_device.pci_device;
    if (block_path != NULL) {
        status = block_init(&block, block_path, addr, kVmoSize);
        if (status != MX_OK)
            return status;

        status = pci_bus_connect(&bus, virtio_block, PCI_DEVICE_VIRTIO_BLOCK);
        if (status != MX_OK)
            return status;

        status = pci_device_async(virtio_block, guest);
        if (status != MX_OK)
            return status;
    }
    // Setup memory balloon.
    balloon_t balloon;
    balloon_init(&balloon, addr, kVmoSize, physmem_vmo);
    balloon.deflate_on_demand = balloon_deflate_on_demand;
    status = pci_bus_connect(&bus, &balloon.virtio_device.pci_device,
                             PCI_DEVICE_VIRTIO_BALLOON);
    if (status != MX_OK)
        return status;
    status = pci_device_async(&balloon.virtio_device.pci_device, guest);
    if (status != MX_OK)
        return status;
    if (balloon_poll_interval > 0)
        poll_balloon_stats(&balloon, balloon_poll_interval);

    vcpu_ctx_t vcpu_ctx;
    vcpu_init(&vcpu_ctx);
    vcpu_ctx.vcpu = vcpu;
#if __x86_64__
    vcpu_ctx.local_apic.apic_addr = (void*)apic_addr;
#endif // __x86_64__
    vcpu_ctx.guest_ctx = &guest_ctx;
    // Setup Local APIC.
    vcpu_ctx.local_apic.vcpu = vcpu;
    status = io_apic_register_local_apic(&io_apic, 0, &vcpu_ctx.local_apic);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to register Local APIC with IO APIC.\n");
        return status;
    }

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
