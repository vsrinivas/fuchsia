// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/virtio.h>

#define SECTOR_SIZE 512u

typedef struct guest_state guest_state_t;
typedef struct io_apic io_apic_t;
typedef struct mx_guest_io mx_guest_io_t;
typedef struct mx_vcpu_io mx_vcpu_io_t;

/* Stores the state of a block device. */
typedef struct block {
    // File descriptor backing the block device.
    int fd;
    // Size of file backing the block device.
    uint64_t size;

    // Address of guest physical memory.
    void* guest_physmem_addr;
    // Size of guest physical memory.
    size_t guest_physmem_size;
    // IO APIC for use with interrupt redirects.
    io_apic_t* io_apic;

    // Virtio feature flags.
    uint32_t features;
    // Virtio status register for the block device.
    uint8_t status;
    // Virtio queue for the block device.
    virtio_queue_t queue;
} block_t;

mx_status_t block_init(block_t* block, const char* block_path, void* guest_physmem_addr,
                       size_t guest_physmem_size, io_apic_t* io_apic);
mx_status_t block_read(const block_t* block, uint16_t port, mx_vcpu_io_t* vcpu_io);
mx_status_t block_write(block_t* block, mx_handle_t vcpu, uint16_t port, const mx_guest_io_t* io);

/* Block device that returns zeros when read, and ignores all writes. */
mx_status_t null_block_device(block_t* block, void* guest_physmem_addr, size_t guest_physmem_size);

/* Block device that returns reads and writes to a file. */
mx_status_t file_block_device(block_t* block, void* guest_physmem_addr, size_t guest_physmem_size);
