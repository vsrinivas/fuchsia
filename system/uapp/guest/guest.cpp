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

#include <fbl/unique_ptr.h>
#include <hypervisor/acpi.h>
#include <hypervisor/address.h>
#include <hypervisor/balloon.h>
#include <hypervisor/block.h>
#include <hypervisor/gpu.h>
#include <hypervisor/guest.h>
#include <hypervisor/input.h>
#include <hypervisor/io_apic.h>
#include <hypervisor/io_port.h>
#include <hypervisor/pci.h>
#include <hypervisor/tpm.h>
#include <hypervisor/uart.h>
#include <hypervisor/vcpu.h>
#include <virtio/balloon.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include "linux.h"
#include "zircon.h"

static const uint64_t kVmoSize = 1u << 30;

/* Unused memory above this threshold may be reclaimed by the balloon. */
static uint32_t balloon_threshold_pages = 1024;

static zx_status_t usage(const char* cmd) {
    fprintf(stderr, "usage: %s [OPTIONS] kernel.bin\n", cmd);
    fprintf(stderr, "\n");
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "\t-b [block.bin]     Use file 'block.bin' as a virtio-block device\n");
    fprintf(stderr, "\t-r [ramdisk.bin]   Use file 'ramdisk.bin' as a ramdisk\n");
    fprintf(stderr, "\t-c [cmdline]       Use string 'cmdline' as the kernel command line\n");
    fprintf(stderr, "\t-m [seconds]       Poll the virtio-balloon device every 'seconds' seconds\n"
                    "\t                   and adjust the balloon size based on the amount of\n"
                    "\t                   unused guest memory\n");
    fprintf(stderr, "\t-p [pages]         Number of unused pages to allow the guest to\n"
                    "\t                   retain. Has no effect unless -m is also used\n");
    fprintf(stderr, "\t-d                 Demand-page balloon deflate requests\n");
    fprintf(stderr, "\t-g                 Enable graphics output to the framebuffer.\n");
    fprintf(stderr, "\n");
    return ZX_ERR_INVALID_ARGS;
}

static zx_status_t create_vmo(uint64_t size, uintptr_t* addr, zx_handle_t* vmo) {
    zx_status_t status = zx_vmo_create(size, 0, vmo);
    if (status != ZX_OK)
        return status;
    return zx_vmar_map(zx_vmar_root_self(), 0, *vmo, 0, size,
                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, addr);
}

static void balloon_stats_handler(VirtioBalloon* balloon, const virtio_balloon_stat_t* stats,
                                  size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (stats[i].tag != VIRTIO_BALLOON_S_AVAIL)
            continue;

        uint32_t current_pages = balloon->num_pages();
        uint32_t available_pages = static_cast<uint32_t>(stats[i].val / VirtioBalloon::kPageSize);
        uint32_t target_pages = current_pages + (available_pages - balloon_threshold_pages);
        if (current_pages == target_pages)
            return;

        printf("virtio-balloon: adjusting target pages %#x -> %#x\n",
               current_pages, target_pages);
        zx_status_t status = balloon->UpdateNumPages(target_pages);
        if (status != ZX_OK)
            fprintf(stderr, "Error %d updating balloon size\n", status);
        return;
    }
}

typedef struct balloon_task_args {
    VirtioBalloon* balloon;
    zx_duration_t interval;
} balloon_task_args_t;

static int balloon_stats_task(void* ctx) {
    fbl::unique_ptr<balloon_task_args_t> args(static_cast<balloon_task_args_t*>(ctx));
    VirtioBalloon* balloon = args->balloon;
    while (true) {
        zx_nanosleep(zx_deadline_after(args->interval));
        args->balloon->RequestStats([balloon](const virtio_balloon_stat_t* stats, size_t len) {
            balloon_stats_handler(balloon, stats, len);
        });
    }
    return ZX_OK;
}

static zx_status_t poll_balloon_stats(VirtioBalloon* balloon, zx_duration_t interval) {
    thrd_t thread;
    auto args = new balloon_task_args_t{balloon, interval};

    int ret = thrd_create(&thread, balloon_stats_task, args);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create balloon thread %d\n", ret);
        delete args;
        return ZX_ERR_INTERNAL;
    }

    ret = thrd_detach(thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach balloon thread %d\n", ret);
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

int main(int argc, char** argv) {
    const char* cmd = basename(argv[0]);
    const char* block_path = NULL;
    const char* ramdisk_path = NULL;
    const char* cmdline = NULL;
    zx_duration_t balloon_poll_interval = 0;
    bool balloon_deflate_on_demand = false;
    bool use_gpu = false;
    int opt;
    while ((opt = getopt(argc, argv, "b:r:c:m:dp:g")) != -1) {
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
            balloon_poll_interval = ZX_SEC(strtoul(optarg, nullptr, 10));
            if (balloon_poll_interval <= 0) {
                fprintf(stderr, "Invalid balloon interval %s. Must be an integer greater than 0\n",
                        optarg);
                return ZX_ERR_INVALID_ARGS;
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
                return ZX_ERR_INVALID_ARGS;
            }
            break;
        case 'g':
            use_gpu = true;
            break;
        default:
            return usage(cmd);
        }
    }
    if (optind >= argc)
        return usage(cmd);
    argc -= optind;
    argv += optind;

    Guest guest;
    zx_status_t status = guest.Init(kVmoSize);
    if (status != ZX_OK)
        return status;

    uintptr_t physmem_addr = guest.phys_mem().addr();
    size_t physmem_size = guest.phys_mem().size();
    uintptr_t pt_end_off;
    status = guest.CreatePageTable(&pt_end_off);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create page table\n");
        return status;
    }

    status = guest_create_acpi_table(physmem_addr, physmem_size, pt_end_off);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create ACPI table\n");
        return status;
    }

    // Prepare the OS image
    int fd = open(argv[0], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open kernel image \"%s\"\n", argv[0]);
        return ZX_ERR_IO;
    }

    // Load the first page in to allow OS detection without requiring
    // us to seek backwards later.
    uintptr_t first_page = physmem_addr + physmem_size - PAGE_SIZE;
    ssize_t ret = read(fd, (void*)first_page, PAGE_SIZE);
    if (ret != PAGE_SIZE) {
        fprintf(stderr, "Failed to read first page of kernel\n");
        return ZX_ERR_IO;
    }

    uintptr_t guest_ip;
    uintptr_t bootdata_off = 0;

    char guest_cmdline[PATH_MAX];
    const char* zircon_fmt_string = "TERM=uart %s";
    snprintf(guest_cmdline, PATH_MAX, zircon_fmt_string, cmdline ? cmdline : "");
    status = setup_zircon(physmem_addr, physmem_size, first_page, pt_end_off, fd, ramdisk_path,
                          guest_cmdline, &guest_ip, &bootdata_off);

    if (status == ZX_ERR_NOT_SUPPORTED) {
        const char* linux_fmt_string = "earlyprintk=serial,ttyS,115200 console=ttyS0,115200 "
                                       "io_delay=none acpi_rsdp=%#lx clocksource=tsc %s";
        snprintf(guest_cmdline, PATH_MAX, linux_fmt_string, pt_end_off, cmdline ? cmdline : "");
        status = setup_linux(physmem_addr, physmem_size, first_page, fd, ramdisk_path,
                             guest_cmdline, &guest_ip, &bootdata_off);
    }
    if (status == ZX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Unknown kernel\n");
        return status;
    } else if (status != ZX_OK) {
        fprintf(stderr, "Failed to load kernel\n");
        return status;
    }
    close(fd);

#if __x86_64__
    uintptr_t apic_addr;
    zx_handle_t apic_vmo;
    status = create_vmo(PAGE_SIZE, &apic_addr, &apic_vmo);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create VCPU local APIC memory\n");
        return status;
    }
#endif // __x86_64__

    zx_vcpu_create_args_t args = {
        guest_ip,
#if __x86_64__
        0 /* cr3 */,
        apic_vmo,
#endif // __x86_64__
    };
    zx_handle_t vcpu;
    status = zx_vcpu_create(guest.handle(), 0, &args, &vcpu);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create VCPU\n");
        return status;
    }

    // Setup IO APIC.
    IoApic io_apic;
    guest.io_apic = &io_apic;
#if __x86_64__
    // Setup IO ports.
    IoPort io_port;
    status = io_port.Init(&guest);
    if (status != ZX_OK)
        return status;
    // Setup TPM
    TpmHandler tpm;
    status = tpm.Init(&guest);
    if (status != ZX_OK)
        return status;
#endif
    // Setup PCI.
    PciBus bus(guest.handle(), &io_apic);
    guest.pci_bus = &bus;
    status = bus.Init();
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create PCI bus\n");
        return status;
    }
    // Setup UART.
    Uart uart(&io_apic);
    status = uart.Start(&guest);
    if (status != ZX_OK)
        return status;

    // Setup block device.
    VirtioBlock block(physmem_addr, physmem_size);
    PciDevice& virtio_block = block.pci_device();
    if (block_path != NULL) {
        status = block.Init(block_path);
        if (status != ZX_OK)
            return status;

        status = bus.Connect(&virtio_block, PCI_DEVICE_VIRTIO_BLOCK);
        if (status != ZX_OK)
            return status;
    }
    // Setup memory balloon.
    VirtioBalloon balloon(physmem_addr, physmem_size, guest.phys_mem().vmo());
    balloon.set_deflate_on_demand(balloon_deflate_on_demand);
    status = bus.Connect(&balloon.pci_device(), PCI_DEVICE_VIRTIO_BALLOON);
    if (status != ZX_OK)
        return status;
    if (balloon_poll_interval > 0)
        poll_balloon_stats(&balloon, balloon_poll_interval);
    // Setup Virtio GPU.
    VirtioGpu gpu(physmem_addr, physmem_size);
    VirtioInput input(physmem_addr, physmem_size, "zircon-input", "serial-number");
    if (use_gpu) {
        status = gpu.Init("/dev/class/framebuffer/000");
        if (status != ZX_OK)
            return status;

        status = bus.Connect(&gpu.pci_device(), PCI_DEVICE_VIRTIO_GPU);
        if (status != ZX_OK)
            return status;

        // Setup input device.
        status = input.Start();
        if (status != ZX_OK)
            return status;
        status = bus.Connect(&input.pci_device(), PCI_DEVICE_VIRTIO_INPUT);
        if (status != ZX_OK)
            return status;
    }

    // TODO: Move APIC initialization into a Vcpu factory method. This
    // reduces the need for platform switches here.
    vcpu_ctx_t vcpu_ctx(vcpu
#if __x86_64__
        , apic_addr
#endif
    );
    vcpu_ctx.guest = &guest;
    // Setup Local APIC.
    status = io_apic.RegisterLocalApic(0, &vcpu_ctx.local_apic);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to register Local APIC with IO APIC\n");
        return status;
    }

    zx_vcpu_state_t vcpu_state;
    memset(&vcpu_state, 0, sizeof(vcpu_state));
#if __x86_64__
    vcpu_state.rsi = bootdata_off;
#endif // __x86_64__
    status = zx_vcpu_write_state(vcpu, ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to write VCPU state\n");
        return status;
    }

    return vcpu_loop(&vcpu_ctx);
}
