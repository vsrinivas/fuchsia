// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/guest.h>

#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/type_support.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

static const char kResourcePath[] = "/dev/misc/sysinfo";

// Number of threads reading from the async device port.
static const size_t kNumAsyncWorkers = 1;

static zx_status_t guest_get_resource(zx_handle_t* resource) {
    int fd = open(kResourcePath, O_RDWR);
    if (fd < 0)
        return ZX_ERR_IO;
    ssize_t n = ioctl_sysinfo_get_hypervisor_resource(fd, resource);
    close(fd);
    return n < 0 ? ZX_ERR_IO : ZX_OK;
}

zx_status_t Guest::Init(size_t mem_size) {
    zx_status_t status = phys_mem_.Init(mem_size);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create guest physical memory\n");
        return status;
    }

    zx_handle_t resource;
    status = guest_get_resource(&resource);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to get hypervisor resource\n");
        return status;
    }

    status = zx_guest_create(resource, 0, phys_mem_.vmo(), &guest_);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create guest\n");
        return status;
    }
    zx_handle_close(resource);

    status = zx::port::create(0, &port_);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create port\n");
        return status;
    }

    for (size_t i = 0; i < kNumAsyncWorkers; ++i) {
        thrd_t thread;
        auto thread_func = +[](void* arg) { return static_cast<Guest*>(arg)->IoThread(); };
        int ret = thrd_create_with_name(&thread, thread_func, this, "io-handler");
        if (ret != thrd_success) {
            fprintf(stderr, "Failed to create io handler thread: %d\n", ret);
            return ZX_ERR_INTERNAL;
        }

        ret = thrd_detach(thread);
        if (ret != thrd_success) {
            fprintf(stderr, "Failed to detach io handler thread: %d\n", ret);
            return ZX_ERR_INTERNAL;
        }
    }

    return ZX_OK;
}

Guest::~Guest() {
    zx_handle_close(guest_);
}

zx_status_t Guest::IoThread() {
    while (true) {
        zx_port_packet_t packet;
        zx_status_t status = port_.wait(zx::time::infinite(), &packet, 0);
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to wait for device port %d\n", status);
            break;
        }

        uint64_t addr;
        IoValue value;
        switch (packet.type) {
        case ZX_PKT_TYPE_GUEST_IO:
            addr = packet.guest_io.port;
            value.access_size = packet.guest_io.access_size;
            static_assert(sizeof(value.data) >= sizeof(packet.guest_io.data),
                          "IoValue too small to contain zx_packet_guest_io_t.");
            memcpy(value.data, packet.guest_io.data, sizeof(packet.guest_io.data));
            break;
        case ZX_PKT_TYPE_GUEST_BELL:
            addr = packet.guest_bell.addr;
            value.access_size = 0;
            value.u32 = 0;
            break;
        default:
            return ZX_ERR_NOT_SUPPORTED;
        }

        status = trap_key_to_mapping(packet.key)->Write(addr, value);
        if (status != ZX_OK) {
            fprintf(stderr, "Unable to handle packet for device %d\n", status);
            break;
        }
    }

    return ZX_ERR_INTERNAL;
}

static constexpr uint32_t trap_kind(TrapType type) {
    switch (type) {
    case TrapType::MMIO_SYNC:
        return ZX_GUEST_TRAP_MEM;
    case TrapType::MMIO_BELL:
        return ZX_GUEST_TRAP_BELL;
    case TrapType::PIO_SYNC:
    case TrapType::PIO_ASYNC:
        return ZX_GUEST_TRAP_IO;
    default:
        ZX_PANIC("Unhandled TrapType %d\n",
                 static_cast<fbl::underlying_type<TrapType>::type>(type));
        return 0;
    }
}

static constexpr zx_handle_t get_trap_port(TrapType type, zx_handle_t port) {
    switch (type) {
    case TrapType::PIO_ASYNC:
    case TrapType::MMIO_BELL:
        return port;
    case TrapType::PIO_SYNC:
    case TrapType::MMIO_SYNC:
        return ZX_HANDLE_INVALID;
    default:
        ZX_PANIC("Unhandled TrapType %d\n",
                 static_cast<fbl::underlying_type<TrapType>::type>(type));
        return ZX_HANDLE_INVALID;
    }
}

zx_status_t Guest::CreateMapping(TrapType type, uint64_t addr, size_t size, uint64_t offset,
                                 IoHandler* handler) {
    fbl::AllocChecker ac;
    auto mapping = fbl::make_unique_checked<IoMapping>(&ac, addr, size, offset, handler);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    // Set a trap for the IO region. We set the 'key' to be the address of the
    // mapping so that we get the pointer to the mapping provided to us in port
    // packets.
    zx_handle_t port = get_trap_port(type, port_.get());
    uint32_t kind = trap_kind(type);
    uint64_t key = reinterpret_cast<uintptr_t>(mapping.get());
    zx_status_t status = zx_guest_set_trap(guest_, kind, addr, size, port, key);
    if (status != ZX_OK)
        return status;

    mappings_.push_front(fbl::move(mapping));
    return ZX_OK;
}

void Guest::RegisterVcpuFactory(VcpuFactory factory) {
    vcpu_factory_ = fbl::move(factory);
}

zx_status_t Guest::StartVcpu(uintptr_t entry, uint64_t id) {
    fbl::AutoLock lock(&mutex_);
    if (id >= kMaxVcpus) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (vcpus_[0] == nullptr && id != 0) {
        fprintf(stderr, "VCPU-0 must be started before other VCPUs\n");
        return ZX_ERR_BAD_STATE;
    }
    if (vcpus_[id] != nullptr) {
        return ZX_ERR_ALREADY_EXISTS;
    }
    auto vcpu = fbl::make_unique<Vcpu>();
    zx_status_t status = vcpu_factory_(this, entry, id, vcpu.get());
    if (status != ZX_OK) {
        return status;
    }
    vcpus_[id] = fbl::move(vcpu);

    return ZX_OK;
}

zx_status_t Guest::Join() {
    // We assume that the VCPU-0 thread will be started first, and that no additional VCPUs will
    // be brought up after it terminates.
    zx_status_t status = vcpus_[0]->Join();

    // Once the initial VCPU has terminated, wait for any additional VCPUs.
    for (size_t id = 1; id != kMaxVcpus; ++id) {
        if (vcpus_[id] != nullptr) {
            zx_status_t vcpu_status = vcpus_[id]->Join();
            if (vcpu_status != ZX_OK) {
                status = vcpu_status;
            }
        }
    }

    return status;
}
