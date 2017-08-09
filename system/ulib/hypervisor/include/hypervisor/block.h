// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/virtio.h>

#define SECTOR_SIZE 512u

typedef struct io_apic io_apic_t;

/* Stores the state of a block device. */
typedef struct block {
    // File descriptor backing the block device.
    int fd;
    // Size of file backing the block device.
    uint64_t size;
    // Common virtio device state.
    virtio_device_t virtio_device;
    // Queue for handling block requests.
    virtio_queue_t queue;
} block_t;

/* Perform basic structure initialization that cannot fail. */
void block_null_init(block_t* block, void* guest_physmem_addr,
                     size_t guest_physmem_size, io_apic_t* io_apic);

mx_status_t block_init(block_t* block, const char* block_path, void* guest_physmem_addr,
                       size_t guest_physmem_size, io_apic_t* io_apic);

/* Block device that returns zeros when read, and ignores all writes. */
mx_status_t null_block_device(block_t* block);

/* Block device that returns reads and writes to a file. */
mx_status_t file_block_device(block_t* block);
