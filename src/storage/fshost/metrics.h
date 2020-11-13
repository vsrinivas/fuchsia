// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_METRICS_H_
#define SRC_STORAGE_FSHOST_METRICS_H_

#include <lib/async/dispatcher.h>
#include <zircon/compiler.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
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
  FsHostMetrics(FsHostMetrics&&) = delete;
  FsHostMetrics& operator=(const FsHostMetrics&) = delete;
  FsHostMetrics& operator=(FsHostMetrics&&) = delete;
  ~FsHostMetrics();

  // This method logs an event describing a corrupted MinFs filesystem, detected on mount or fsck.
  void LogMinfsCorruption();

  // Repeatedly attempt to flush to cobalt until success.
  //
  // Retries every 10 seconds.
  // The retry is done async.
  void Flush();

  // Returns a pointer to the underlying |cobalt_client::Collector| instance.
  cobalt_client::Collector* mutable_collector() { return collector_.get(); }

 private:
  // Sleep duration between two successive attempts to flush metrics.
  static constexpr std::chrono::nanoseconds kSleepDuration = std::chrono::seconds(10);

  void Run();

  std::unique_ptr<cobalt_client::Collector> collector_;
  std::unordered_map<fs_metrics::Event, std::unique_ptr<cobalt_client::Counter>> counters_;

  std::mutex mutex_;

  // A way to wakeup sleeping |thread_| when object is getting destroyed.
  std::condition_variable_any condition_;

  // True if destructor is called.
  bool shut_down_ __TA_GUARDED(mutex_) = false;

  // True if |thread_| should try to flush metrics.
  bool flush_ __TA_GUARDED(mutex_) = false;

  // Thread which periodically flushes metrics.
  std::thread thread_;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_METRICS_H_
