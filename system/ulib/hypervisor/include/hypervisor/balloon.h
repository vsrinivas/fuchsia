// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <hypervisor/virtio.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <virtio/balloon.h>

#define VIRTIO_BALLOON_Q_INFLATEQ 0
#define VIRTIO_BALLOON_Q_DEFLATEQ 1
#define VIRTIO_BALLOON_Q_COUNT 2

/* Virtio memory balloon device. */
typedef struct balloon {
    mtx_t mutex;
    // Handle to the guest phsycial memory VMO for memory management.
    mx_handle_t vmo;

    virtio_device_t virtio_device;
    virtio_queue_t queues[VIRTIO_BALLOON_Q_COUNT];
    virtio_balloon_config_t config;
} balloon_t;

void balloon_init(balloon_t* balloon, void* guest_physmem_addr, size_t guest_physmem_size,
                  mx_handle_t guest_physmem_vmo);
