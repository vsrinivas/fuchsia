// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <hypervisor/virtio.h>

#define SECTOR_SIZE 512u

typedef struct io_apic io_apic_t;

/* Stores the state of a block device. */
typedef struct block {
    mtx_t mutex;

    // File descriptor backing the block device.
    int fd;
    // Size of file backing the block device.
    uint64_t size;

    // Common virtio device state.
    virtio_device_t virtio_device;
    // Queue for handling block requests.
    virtio_queue_t queue;
} block_t;

mx_status_t block_init(block_t* block, const char* path, void* guest_physmem_addr,
                       size_t guest_physmem_size);

/* Block device that returns reads and writes to a file. */
mx_status_t file_block_device(block_t* block);
