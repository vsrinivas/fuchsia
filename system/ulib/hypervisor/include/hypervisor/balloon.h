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
#define VIRTIO_BALLOON_Q_STATSQ 2
#define VIRTIO_BALLOON_Q_COUNT 3

/* Per Virtio 1.0 Section 5.5.6, This value is historical, and independent of
 * the guest page size.
 */
#define VIRTIO_BALLOON_PAGE_SIZE 4096

__BEGIN_CDECLS

/* Virtio memory balloon device. */
typedef struct balloon {
    mtx_t mutex;
    // Handle to the guest phsycial memory VMO for memory management.
    mx_handle_t vmo;
    // With on-demand deflation we won't commit memory up-front for balloon
    // deflate requests.
    bool deflate_on_demand;

    struct {
        // The index in the available ring of the stats descriptor.
        uint16_t desc_index;
        // Indicates if desc_index valid.
        bool has_buffer;
        // Holds exclusive access to the stats queue. At most one stats request
        // can be active at a time (by design). Specifically we need to hold
        // exclusive access of the queue from the time a buffer is returned to
        // the queue, initiating a stats request, until any logic processing
        // the result has finished.
        //
        // Also guards access to other members of this structure.
        mtx_t mutex;
    } stats;

    virtio_device_t virtio_device;
    virtio_queue_t queues[VIRTIO_BALLOON_Q_COUNT];
    virtio_balloon_config_t config;
} balloon_t;

void balloon_init(balloon_t* balloon, uintptr_t guest_physmem_addr, size_t guest_physmem_size,
                  mx_handle_t guest_physmem_vmo);

/* Callback for balloon_request_stats. */
typedef void (*balloon_stats_fn_t)(const virtio_balloon_stat_t* stats, size_t len, void* ctx);

/* Request balloon memory statistics from the guest.
 *
 * The callback will be executed syncronously with this thread once stats have
 * been received from the guest. Pointers to stats must not be held after the
 * callback returns.
 */
mx_status_t balloon_request_stats(balloon_t* balloon, balloon_stats_fn_t handler, void* ctx);

/* Update the 'num_pages' configuration field in the balloon. */
mx_status_t balloon_update_num_pages(balloon_t* balloon, uint32_t num_pages);

__END_CDECLS
