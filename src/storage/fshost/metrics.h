// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_METRICS_H_
#define SRC_STORAGE_FSHOST_METRICS_H_

#include <memory>

namespace fshost {

class FsHostMetrics {
 public:
  FsHostMetrics() = default;
  FsHostMetrics(const FsHostMetrics&) = delete;
  FsHostMetrics(FsHostMetrics&&) = delete;
  FsHostMetrics& operator=(const FsHostMetrics&) = delete;
  FsHostMetrics& operator=(FsHostMetrics&&) = delete;
  virtual ~FsHostMetrics() = default;

  // This method logs an event describing a corrupted MinFs filesystem, detected on mount or fsck.
  virtual void LogMinfsCorruption() = 0;

  // Repeatedly attempt to flush to cobalt until success.
  //
  // Retries every 10 seconds.
  // The retry is done async.
  virtual void Flush() = 0;
};

std::unique_ptr<FsHostMetrics> DefaultMetrics();

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_METRICS_H_
