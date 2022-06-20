// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_METRICS_VERIFICATION_METRICS_H_
#define SRC_STORAGE_BLOBFS_METRICS_VERIFICATION_METRICS_H_

#include <lib/zx/time.h>
#include <zircon/compiler.h>

#include <mutex>

#include "src/lib/storage/vfs/cpp/ticker.h"

namespace blobfs {

// The |VerificationMetrics| class tracks blobfs metrics related to Merkle verification of blobs,
// both on blob reads and on blob writes.
//
// This class is thread-safe.
class VerificationMetrics {
 public:
  VerificationMetrics() = default;
  VerificationMetrics(const VerificationMetrics&) = delete;
  VerificationMetrics& operator=(const VerificationMetrics&) = delete;

  // Increments aggregate information about Merkle verification of blobs since mounting.
  void Increment(uint64_t data_size, uint64_t merkle_size, fs::Duration duration)
      __TA_EXCLUDES(mutex_);

  struct Snapshot {
    uint64_t blobs_verified = {};
    uint64_t data_size = {};
    uint64_t merkle_size = {};
    zx_ticks_t verification_time = {};
  };

  // Returns a snapshot of the metrics.
  Snapshot Get() const __TA_EXCLUDES(mutex_);

 private:
  uint64_t blobs_verified_ __TA_GUARDED(mutex_) = {};
  uint64_t blobs_verified_total_size_data_ __TA_GUARDED(mutex_) = {};
  uint64_t blobs_verified_total_size_merkle_ __TA_GUARDED(mutex_) = {};
  zx::ticks total_verification_time_ticks_ __TA_GUARDED(mutex_) = {};

  mutable std::mutex mutex_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_METRICS_VERIFICATION_METRICS_H_
