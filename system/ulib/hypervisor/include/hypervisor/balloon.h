// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <fbl/function.h>
#include <fbl/mutex.h>
#include <hypervisor/virtio.h>
#include <virtio/balloon.h>
#include <zircon/compiler.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#define VIRTIO_BALLOON_Q_INFLATEQ 0
#define VIRTIO_BALLOON_Q_DEFLATEQ 1
#define VIRTIO_BALLOON_Q_STATSQ 2
#define VIRTIO_BALLOON_Q_COUNT 3

/* Virtio memory balloon device. */
class VirtioBalloon : public VirtioDevice {
public:
    // Per Virtio 1.0 Section 5.5.6, This value is historical, and independent
    // of the guest page size.
    static const uint32_t kPageSize = 4096;

    VirtioBalloon(uintptr_t guest_physmem_addr, size_t guest_physmem_size,
                  zx_handle_t guest_physmem_vmo);
    ~VirtioBalloon() override = default;

    zx_status_t HandleQueueNotify(uint16_t queue_sel) override;

    // Receives an array of statistics in |stats| that contains |len| entries.
    //
    // The pointers backing |stats| are only guaranteed to live for the
    // duration of this callback.
    using StatsHandler = fbl::Function<void(const virtio_balloon_stat_t* stats, size_t len)>;

    // Request balloon memory statistics from the guest.
    //
    // Sends a message to the driver that memory stats are requested. Once the
    // driver has provided the statistics, the handler will be invoked.
    //
    // This method blocks for the entire duration of the request.
    zx_status_t RequestStats(StatsHandler handler);

    // Update the 'num_pages' configuration field in the balloon.
    //
    // If the value is greater than what it currently is, the driver should
    // provided pages to us. If the value is less than what it currently is,
    // driver is free to reclaim memory from the balloon.
    zx_status_t UpdateNumPages(uint32_t num_pages);

    // Read the 'num_pages' configuration field.
    uint32_t num_pages();

    // If deflate on demand is enabled, then the balloon will treat deflate
    // requests as a no-op. This memory will instead be provided via demand
    // paging.
    void set_deflate_on_demand(bool b) { deflate_on_demand_ = b; }

private:
    void WaitForStatsBuffer(virtio_queue_t* stats_queue) TA_REQ(stats_.mutex);

    zx_status_t HandleDescriptor(uint16_t queue_sel);

    // Handle to the guest phsycial memory VMO for memory management.
    zx_handle_t vmo_ = ZX_HANDLE_INVALID;

    // With on-demand deflation we won't commit memory up-front for balloon
    // deflate requests.
    bool deflate_on_demand_ = false;

    struct {
        // The index in the available ring of the stats descriptor.
        uint16_t desc_index TA_GUARDED(mutex) = 0;
        // Indicates if desc_index valid.
        bool has_buffer TA_GUARDED(mutex) = false;
        // Holds exclusive access to the stats queue. At most one stats request
        // can be active at a time (by design). Specifically we need to hold
        // exclusive access of the queue from the time a buffer is returned to
        // the queue, initiating a stats request, until any logic processing
        // the result has finished.
        //
        // Also guards access to other members of this structure.
        fbl::Mutex mutex;
    } stats_;

    virtio_queue_t queues_[VIRTIO_BALLOON_Q_COUNT];

    virtio_balloon_config_t config_ TA_GUARDED(config_mutex_) = {};
};
