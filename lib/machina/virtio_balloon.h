// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_BALLOON_H_
#define GARNET_LIB_MACHINA_VIRTIO_BALLOON_H_

#include <fbl/mutex.h>
#include <lib/fit/function.h>
#include <virtio/balloon.h>
#include <virtio/virtio_ids.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "garnet/lib/machina/virtio_device.h"

#define VIRTIO_BALLOON_Q_INFLATEQ 0
#define VIRTIO_BALLOON_Q_DEFLATEQ 1
#define VIRTIO_BALLOON_Q_STATSQ 2
#define VIRTIO_BALLOON_Q_COUNT 3

namespace machina {

// Virtio memory balloon device.
class VirtioBalloon
    : public VirtioDeviceBase<VIRTIO_ID_BALLOON, VIRTIO_BALLOON_Q_COUNT,
                              virtio_balloon_config_t> {
 public:
  // Per Virtio 1.0 Section 5.5.6, This value is historical, and independent
  // of the guest page size.
  static constexpr uint32_t kPageSize = 4096;

  VirtioBalloon(const PhysMem& phys_mem);
  ~VirtioBalloon() override = default;

  zx_status_t HandleQueueNotify(uint16_t queue_sel) override;

  // Receives an array of statistics in |stats| that contains |len| entries.
  //
  // The pointers backing |stats| are only guaranteed to live for the
  // duration of this callback.
  using StatsHandler =
      fit::function<void(const virtio_balloon_stat_t* stats, size_t len)>;

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
  void WaitForStatsBuffer(VirtioQueue* stats_queue) __TA_REQUIRES(stats_.mutex);

  zx_status_t HandleDescriptor(uint16_t queue_sel);

  // With on-demand deflation we won't commit memory up-front for balloon
  // deflate requests.
  bool deflate_on_demand_ = false;

  struct {
    // The index in the available ring of the stats descriptor.
    uint16_t desc_index __TA_GUARDED(mutex) = 0;
    // Indicates if desc_index valid.
    bool has_buffer __TA_GUARDED(mutex) = false;
    // Holds exclusive access to the stats queue. At most one stats request
    // can be active at a time (by design). Specifically we need to hold
    // exclusive access of the queue from the time a buffer is returned to
    // the queue, initiating a stats request, until any logic processing
    // the result has finished.
    //
    // Also guards access to other members of this structure.
    fbl::Mutex mutex;
  } stats_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_BALLOON_H_
