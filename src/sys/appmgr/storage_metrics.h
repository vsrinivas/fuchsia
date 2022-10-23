// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_STORAGE_METRICS_H_
#define SRC_SYS_APPMGR_STORAGE_METRICS_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/zx/result.h>
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

    const std::unordered_map<std::string, Usage>& map() const { return map_; }

   private:
    std::unordered_map<std::string, Usage> map_;
  };

  // paths_to_watch takes the list of file paths to watch from the rootof appmgr's namespace, and
  // inspect_node will be the root of the inspect tree for this set of metrics.
  StorageMetrics(std::vector<std::string> paths_to_watch, inspect::Node inspect_node);

  // Should be called exactly once to begin the periodic aggregation, with one being scheduled
  // immediately. Call this only once.
  zx::result<> Run();

 private:
  static constexpr zx::duration kPollCycle = zx::min(60);

  friend class StorageMetricsTest;

  // Populate the inspect node for byte usage using the results from the last poll.
  fpromise::promise<inspect::Inspector> InspectByteUsage(const std::string& path) const;

  // Populate the inspect node for inode usage using the results from the last poll.
  fpromise::promise<inspect::Inspector> InspectInodeUsage(const std::string& path) const;

  // Perform the actual aggregation.
  std::unordered_map<std::string, StorageMetrics::UsageMap> GatherStorageUsage() const;

  // This is the task that will be scheduled for running, which calls GatherStorageUsage() before
  // scheduling the next run after some delay.
  void PollStorage();

  // The list of paths to watch.
  const std::vector<std::string> paths_to_watch_;

  // The root of the storage metrics inspect tree.
  inspect::Node inspect_root_;

  // A list of bytes used per component, per path, populated on demand.
  inspect::Node inspect_bytes_stats_;

  // A list of inodes used per component, per path, populated on demand.
  inspect::Node inspect_inode_stats_;

  // The list of lazy nodes need to be held somewhere, if they're never directly referenced again.
  std::vector<inspect::LazyNode> lazy_nodes_;

  // Protect the population of the stored usage map between updating and reading from inspect.
  mutable std::mutex usage_lock_;

  // The results of the last poll.
  std::unordered_map<std::string, UsageMap> usage_ __TA_GUARDED(usage_lock_);

  // Manages the lifetime of the thread and dispatcher. This should be last to force the thread
  // shutdown before destructing anything else.
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  DISALLOW_COPY_ASSIGN_AND_MOVE(StorageMetrics);
};

#endif  // SRC_SYS_APPMGR_STORAGE_METRICS_H_
