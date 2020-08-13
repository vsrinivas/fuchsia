// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_VERIFICATION_METRICS_H_
#define SRC_STORAGE_BLOBFS_VERIFICATION_METRICS_H_

#include <lib/zx/time.h>
#include <zircon/compiler.h>

#include <mutex>

#include <fs/ticker.h>

namespace blobfs {

// The |VerificationMetrics| class tracks blobfs metrics related to Merkle verification of blobs,
// both on blob reads and on blob writes.
//
// This class is thread-safe. Blobfs can update these metrics both from the blobfs main thread (when
// reading blobs that cannot be paged, and when writing new blobs), and the userpager thread (when
// reading blobs that support paging).
class VerificationMetrics {
 public:
  VerificationMetrics() = default;
  VerificationMetrics(const VerificationMetrics&) = delete;
  VerificationMetrics& operator=(const VerificationMetrics&) = delete;

  // Increments aggregate information about Merkle verification of blobs
  // since mounting.
  void Increment(uint64_t data_size, uint64_t merkle_size, fs::Duration duration);

  struct Snapshot {
    uint64_t blobs_verified;
    uint64_t data_size;
    uint64_t merkle_size;
    zx_ticks_t verification_time;
  };

  // Returns a snapshot of the metrics.
  Snapshot Get();

 private:
  uint64_t blobs_verified_ __TA_GUARDED(mutex_) = 0;
  uint64_t blobs_verified_total_size_data_ __TA_GUARDED(mutex_) = 0;
  uint64_t blobs_verified_total_size_merkle_ __TA_GUARDED(mutex_) = 0;
  zx::ticks total_verification_time_ticks_ __TA_GUARDED(mutex_) = {};

  std::mutex mutex_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_VERIFICATION_METRICS_H_
