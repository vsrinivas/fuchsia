// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "verification-metrics.h"

namespace blobfs {

void VerificationMetrics::Increment(uint64_t data_size, uint64_t merkle_size,
                                    fs::Duration duration) {
  std::scoped_lock guard(mutex_);
  ++blobs_verified_;
  blobs_verified_total_size_data_ += data_size;
  blobs_verified_total_size_merkle_ += merkle_size;
  total_verification_time_ticks_ += duration;
}

VerificationMetrics::Snapshot VerificationMetrics::Get() {
  std::scoped_lock guard(mutex_);
  return Snapshot{
      .blobs_verified = blobs_verified_,
      .data_size = blobs_verified_total_size_data_,
      .merkle_size = blobs_verified_total_size_merkle_,
      .verification_time = total_verification_time_ticks_.get(),
  };
}

}  // namespace blobfs
