// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Blobfs metrics.

#include "src/storage/blobfs/blobfs_metrics.h"

#include <lib/async/cpp/task.h>
#include <lib/fzl/time.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <string>

#include <fbl/algorithm.h>

#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace blobfs {
namespace {

size_t TicksToMs(const zx::ticks& ticks) { return fzl::TicksToNs(ticks) / zx::msec(1); }

}  // namespace

BlobfsMetrics::BlobfsMetrics(bool should_record_page_in, inspect::Inspector inspector)
    : inspector_{std::move(inspector)}, should_record_page_in_(should_record_page_in) {
  // Add a node that allows querying the size of the Inspect VMO at runtime.
  // TODO(fxbug.dev/80285): Replace the following lazy node with the one now part of the Inspector
  // class itself (i.e. call `inspector_.CreateStatsNode()` instead).
  root_.CreateLazyNode(
      "inspect_vmo_stats",
      [this] {
        inspect::InspectStats stats = inspector_.GetStats();
        inspect::Inspector insp;
        insp.GetRoot().CreateUint("current_size", stats.size, &insp);
        insp.GetRoot().CreateUint("maximum_size", stats.maximum_size, &insp);
        return fpromise::make_result_promise(fpromise::ok(std::move(insp)));
      },
      &inspector_);
}

void BlobfsMetrics::UpdateAllocation(uint64_t size_data, const fs::Duration& duration) {
  blobs_created_property_.Add(1);
  blobs_created_total_size_property_.Add(size_data);
  total_allocation_time_ticks_property_.Add(duration.get());
}

void BlobfsMetrics::UpdateLookup(uint64_t size) {
  blobs_opened_property_.Add(1);
  blobs_opened_total_size_property_.Add(size);
}

void BlobfsMetrics::UpdateClientWrite(uint64_t data_size, uint64_t merkle_size,
                                      const fs::Duration& enqueue_duration,
                                      const fs::Duration& generate_duration) {
  data_bytes_written_property_.Add(data_size);
  merkle_bytes_written_property_.Add(merkle_size);
  total_write_enqueue_time_ticks_property_.Add(enqueue_duration.get());
  total_merkle_generation_time_ticks_property_.Add(generate_duration.get());
}

void BlobfsMetrics::IncrementPageIn(const fbl::String& merkle_hash, uint64_t offset,
                                    uint64_t length) {
  // Page-in metrics are a developer feature that is not intended to be used in production. Enabling
  // this feature also requires increasing the size of the Inspect VMO considerably (>512KB).
  if (!should_record_page_in_) {
    return;
  }

  inspect::InspectStats stats = inspector_.GetStats();
  ZX_ASSERT_MSG(
      stats.maximum_size > stats.size,
      "Blobfs has run out of space in the Inspect VMO. To record page-in metrics accurately, "
      "increase the VMO size. Maximum size: %lu, Current size %lu",
      stats.maximum_size, stats.size);

  std::lock_guard lock(frequencies_lock_);
  if (all_page_in_frequencies_.find(merkle_hash) == all_page_in_frequencies_.end()) {
    // We have no page in metrics on this blob yet. Create a new child node.
    all_page_in_frequencies_[merkle_hash].blob_root_node =
        page_in_frequency_stats_.CreateChild(merkle_hash.c_str());
  }

  BlobPageInFrequencies& blob_frequencies = all_page_in_frequencies_[merkle_hash];

  // Calculate the start+end frame indexes to increment
  uint64_t cur = fbl::round_down(offset, kBlobfsBlockSize) / kBlobfsBlockSize;
  const uint64_t end = fbl::round_up(offset + length, kBlobfsBlockSize) / kBlobfsBlockSize;

  for (; cur < end; cur += 1) {
    if (blob_frequencies.offset_map.find(cur) == blob_frequencies.offset_map.end()) {
      // We have no frequencies recorded at this frame index. Create a new property.
      blob_frequencies.offset_map[cur] =
          blob_frequencies.blob_root_node.CreateUint(std::to_string(cur), 1);
    } else {
      blob_frequencies.offset_map[cur].Add(1);
    }
  }
}

}  // namespace blobfs
