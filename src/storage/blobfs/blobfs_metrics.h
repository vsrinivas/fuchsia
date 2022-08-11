// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Blobfs metrics.

#ifndef SRC_STORAGE_BLOBFS_BLOBFS_METRICS_H_
#define SRC_STORAGE_BLOBFS_BLOBFS_METRICS_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/time.h>

#include <mutex>

#include "src/lib/storage/vfs/cpp/ticker.h"
#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/metrics/read_metrics.h"
#include "src/storage/blobfs/metrics/verification_metrics.h"
#include "src/storage/blobfs/mount.h"

namespace blobfs {

// This struct holds the inspect node for a blob and a map from block index to page-in frequency.
struct BlobPageInFrequencies {
  inspect::Node blob_root_node;
  std::map<uint64_t, inspect::UintProperty> offset_map;
};

// Encapsulates Blobfs-specific metrics available via Inspect.
//
// TODO(fxbug.dev/80285): Make this properly thread-safe.  IncrementPageIn(), paged_read_metrics(),
// unpaged_read_metrics(), and verification_metrics() are not thread safe.
// TODO(fxbug.dev/80285): Make this class encapsulate all Blobfs-specific metrics, and have
// BlobfsInspectTree take ownership of it.
class BlobfsMetrics final {
 public:
  explicit BlobfsMetrics(bool should_record_page_in, inspect::Inspector inspector = {});
  ~BlobfsMetrics() = default;

  // Updates aggregate information about the total number of created blobs since mounting.
  void UpdateAllocation(uint64_t size_data, const fs::Duration& duration);

  // Updates aggregate information about the number of blobs opened since mounting.
  void UpdateLookup(uint64_t size);

  // Updates aggregates information about blobs being written back to blobfs since mounting.
  void UpdateClientWrite(uint64_t data_size, uint64_t merkle_size,
                         const fs::Duration& enqueue_duration,
                         const fs::Duration& generate_duration);

  // Increments the frequency count for blocks in the range [|offset|, |offset| + |length|). This
  // method must only be called from the pager thread.
  // NOTE: This method is a NOP unless |BLOBFS_ENABLE_PAGE_IN_METRICS| compiler flag
  // has been set in the BUILD.gn
  void IncrementPageIn(const fbl::String& merkle_hash, uint64_t offset, uint64_t length);

  // Accessors for ReadMetrics. The metrics objects returned are NOT thread-safe. The metrics
  // objects are to be used by exactly one thread (main or pager). Used to increment relevant
  // metrics from the blobfs main thread and the user pager thread.
  ReadMetrics& paged_read_metrics() { return paged_read_metrics_; }
  ReadMetrics& unpaged_read_metrics() { return unpaged_read_metrics_; }

  // Accessor for VerificationMetrics. This metrics object is thread-safe. Used to increment
  // relevant metrics from the blobfs main thread and the user pager thread.
  // The |BlobfsMetrics| class is not thread-safe except for this accessor.
  VerificationMetrics& verification_metrics() { return verification_metrics_; }

  // Accessor for BlobFS Inspector. This Inspector serves the BlobFS inspect tree.
  inspect::Inspector* inspector() { return &inspector_; }

 private:
  // Inspect instrumentation data.
  inspect::Inspector inspector_;
  inspect::Node& root_ = inspector_.GetRoot();

  // ALLOCATION STATS
  // Created with external-facing "Create".
  uint64_t blobs_created_ = 0;
  // Measured by space allocated with "Truncate".
  uint64_t blobs_created_total_size_ = 0;
  zx::ticks total_allocation_time_ticks_ = {};

  // WRITEBACK STATS
  // Measurements, from the client's perspective, of writing and enqueing
  // data that will later be written to disk.
  uint64_t data_bytes_written_ = 0;
  uint64_t merkle_bytes_written_ = 0;
  zx::ticks total_write_enqueue_time_ticks_ = {};
  zx::ticks total_merkle_generation_time_ticks_ = {};

  // LOOKUP STATS
  // Opened via "LookupBlob".
  uint64_t blobs_opened_ = 0;
  uint64_t blobs_opened_total_size_ = 0;

  // INSPECT NODES AND PROPERTIES
  inspect::Node allocation_stats_ = root_.CreateChild("allocation_stats");
  inspect::Node writeback_stats_ = root_.CreateChild("writeback_stats");
  inspect::Node lookup_stats_ = root_.CreateChild("lookup_stats");
  inspect::Node paged_read_stats_ = root_.CreateChild("paged_read_stats");
  inspect::Node unpaged_read_stats_ = root_.CreateChild("unpaged_read_stats");
  inspect::Node page_in_frequency_stats_ = root_.CreateChild("page_in_frequency_stats");

  // Allocation properties
  inspect::UintProperty blobs_created_property_ =
      allocation_stats_.CreateUint("blobs_created", blobs_created_);
  inspect::UintProperty blobs_created_total_size_property_ =
      allocation_stats_.CreateUint("blobs_created_total_size", blobs_created_total_size_);
  inspect::IntProperty total_allocation_time_ticks_property_ = allocation_stats_.CreateInt(
      "total_allocation_time_ticks", total_allocation_time_ticks_.get());

  // Writeback properties
  inspect::UintProperty data_bytes_written_property_ =
      writeback_stats_.CreateUint("data_bytes_written", data_bytes_written_);
  inspect::UintProperty merkle_bytes_written_property_ =
      writeback_stats_.CreateUint("merkle_bytes_written", merkle_bytes_written_);
  inspect::IntProperty total_write_enqueue_time_ticks_property_ = writeback_stats_.CreateInt(
      "total_write_enqueue_time_ticks", total_write_enqueue_time_ticks_.get());
  inspect::IntProperty total_merkle_generation_time_ticks_property_ = writeback_stats_.CreateInt(
      "total_merkle_generation_time_ticks", total_merkle_generation_time_ticks_.get());

  // Lookup properties
  inspect::UintProperty blobs_opened_property_ =
      lookup_stats_.CreateUint("blobs_opened", blobs_opened_);
  inspect::UintProperty blobs_opened_total_size_property_ =
      lookup_stats_.CreateUint("blobs_opened_total_size", blobs_opened_total_size_);

  // READ STATS
  ReadMetrics paged_read_metrics_{&paged_read_stats_};
  ReadMetrics unpaged_read_metrics_{&unpaged_read_stats_};

  // PAGE-IN FREQUENCY STATS
  const bool should_record_page_in_ = false;
  std::mutex frequencies_lock_;
  std::map<fbl::String, BlobPageInFrequencies> all_page_in_frequencies_
      __TA_GUARDED(frequencies_lock_);

  // VERIFICATION STATS
  VerificationMetrics verification_metrics_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOBFS_METRICS_H_
