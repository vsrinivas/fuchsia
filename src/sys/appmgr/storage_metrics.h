// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_STORAGE_METRICS_H_
#define SRC_SYS_APPMGR_STORAGE_METRICS_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/zx/status.h>
#include <zircon/compiler.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fbl/macros.h>
#include <gtest/gtest_prod.h>

// Monitors parent directories above isolated storage folders to periodically aggregate their total
// space and inode usage, attribute it to the component which owns it, and update those values in
// inspect and Cobalt. This object should not be handled by multiple threads at once.
class StorageMetrics {
 public:
  struct Usage {
    size_t bytes;
    size_t inodes;
  };
  class UsageMap {
   public:
    UsageMap() = default;

    // Adds the passed usage to the existing usage for a name. Creates a new entry if needed.
    void AddForKey(const std::string& name, const Usage& usage);

    const std::unordered_map<std::string, Usage>& map() const { return map_; };

   private:
    std::unordered_map<std::string, Usage> map_;
  };

  // paths_to_watch takes the list of file paths to watch from the rootof appmgr's namespace, and
  // inspect_node will be the root of the inspect tree for this set of metrics.
  StorageMetrics(std::vector<std::string> paths_to_watch, inspect::Node inspect_node);
  DISALLOW_COPY_ASSIGN_AND_MOVE(StorageMetrics);

  // Should be called exactly once to begin the periodic aggregation, with one being scheduled
  // immediately. Call this only once.
  zx::status<> Run();

 private:
  static constexpr zx::duration kPollCycle = zx::min(60);

  friend class StorageMetricsTest;

  // Populate the inspect node for byte usage using the results from the last poll.
  fit::promise<inspect::Inspector> InspectByteUsage() const;

  // Populate the inspect node for inode usage using the results from the last poll.
  fit::promise<inspect::Inspector> InspectInodeUsage() const;

  // Perform the actual aggregation.
  UsageMap GatherStorageUsage() const;

  // This is the task that will be scheduled for running, which calls GatherStorageUsage() before
  // scheduling the next run after some delay.
  void PollStorage();

  // The list of paths to watch.
  const std::vector<std::string> paths_to_watch_;

  // The root of the storage metrics inspect tree.
  inspect::Node inspect_root_;

  // Will be a list of bytes used per component, populated on demand.
  inspect::LazyNode inspect_bytes_stats_;

  // Will be a list of inodes used per component, populated on demand.
  inspect::LazyNode inspect_inode_stats_;

  // Protect the population of the stored usage map between updating and reading from inspect.
  mutable std::mutex usage_lock_;

  // The results of the last poll.
  UsageMap usage_ __TA_GUARDED(usage_lock_);

  // Manages the lifetime of the thread and dispatcher. This should be last to force the thread
  // shutdown before destructing anything else.
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
};

#endif  // SRC_SYS_APPMGR_STORAGE_METRICS_H_
