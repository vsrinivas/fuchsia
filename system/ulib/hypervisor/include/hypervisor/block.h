// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#define SECTOR_SIZE     0x200   // Sector size, 512 bytes

typedef struct guest_state guest_state_t;
typedef struct mx_guest_io mx_guest_io_t;
typedef struct mx_vcpu_io mx_vcpu_io_t;
typedef struct vcpu_context vcpu_context_t;
typedef struct virtio_queue virtio_queue_t;

mx_status_t handle_virtio_block_read(guest_state_t* guest_state, uint16_t port,
                                     mx_vcpu_io_t* vcpu_io);
mx_status_t handle_virtio_block_write(vcpu_context_t* context, uint16_t port,
                                      const mx_guest_io_t* io);

mx_status_t null_block_device(virtio_queue_t* queue, void* mem_addr, size_t mem_size);
mx_status_t file_block_device(virtio_queue_t* queue, void* mem_addr, size_t mem_size, int fd);
