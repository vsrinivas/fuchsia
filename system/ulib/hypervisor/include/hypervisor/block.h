// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/virtio.h>
#include <magenta/types.h>

#define SECTOR_SIZE 512u

/* Stores the state of a block device. */
typedef struct block_state {
    // File descriptor backing the block device.
    int fd;
    // Size of file backing the block device.
    uint64_t size;
    // Virtio status register for the block device.
    uint8_t status;
    // Virtio queue for the block device.
    virtio_queue_t queue;
} block_state_t;

typedef struct guest_state guest_state_t;
typedef struct mx_guest_io mx_guest_io_t;
typedef struct mx_vcpu_io mx_vcpu_io_t;

mx_status_t block_init(block_state_t* block_state, const char* block_path);
mx_status_t block_read(block_state_t* block_state, uint16_t port, mx_vcpu_io_t* vcpu_io);
mx_status_t block_write(guest_state_t* guest_state, mx_handle_t vcpu, uint16_t port,
                        const mx_guest_io_t* io);

/* Block device that returns zeros when read, and ignores all writes. */
mx_status_t null_block_device(virtio_queue_t* queue, void* mem_addr, size_t mem_size);

/* Block device that returns reads and writes to a file. */
mx_status_t file_block_device(virtio_queue_t* queue, void* mem_addr, size_t mem_size, int fd);
