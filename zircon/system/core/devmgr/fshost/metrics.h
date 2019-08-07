// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_METRICS_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_METRICS_H_

#include <memory>
#include <unordered_map>
#include <utility>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter.h>
#include <fs/metrics/events.h>

namespace devmgr {

// This class is an adapter for the cobalt-client interface, which is specialized for
// fshost metrics.
//
// This class is thread-compatible.
class FsHostMetrics {
 public:
  FsHostMetrics() = delete;
  explicit FsHostMetrics(std::unique_ptr<cobalt_client::Collector> collector);
  FsHostMetrics(const FsHostMetrics&) = delete;
  FsHostMetrics(FsHostMetrics&&) = default;
  FsHostMetrics& operator=(const FsHostMetrics&) = delete;
  FsHostMetrics& operator=(FsHostMetrics&&) = delete;
  ~FsHostMetrics();

  // This method logs an event describing a corrupted MinFs filesystem, detected on mount or fsck.
  void LogMinfsCorruption();

  // Returns a pointer to the underlying |cobalt_client::Collector| instance.
  cobalt_client::Collector* mutable_collector() { return collector_.get(); }

 private:
  std::unique_ptr<cobalt_client::Collector> collector_;
  std::unordered_map<fs_metrics::Event, std::unique_ptr<cobalt_client::Counter>> counters_;
};

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_METRICS_H_
