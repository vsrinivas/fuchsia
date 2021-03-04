// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_STORAGE_METRICS_H_
#define SRC_SYS_APPMGR_STORAGE_METRICS_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fbl/macros.h>

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
    void AddForKey(const std::string& name, const Usage& usage);
    const std::unordered_map<std::string, Usage>& map() const { return map_; };

   private:
    std::unordered_map<std::string, Usage> map_;
  };

  explicit StorageMetrics(std::vector<std::string> paths_to_watch);
  DISALLOW_COPY_ASSIGN_AND_MOVE(StorageMetrics);

  // Should be called exactly once to begin the periodic aggregation, with one being scheduled
  // immediately. Call this only once.
  zx_status_t Run();

  // Perform the actual aggregation.
  UsageMap GatherStorageUsage();

 private:
  static constexpr zx::duration kPollCycle = zx::min(60);

  // This is the task that will be scheduled for running, which calls GatherStorageUsage() before
  // scheduling the next run after some delay.
  void PollStorage();

  const std::vector<std::string> paths_to_watch_;
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
};

#endif  // SRC_SYS_APPMGR_STORAGE_METRICS_H_
