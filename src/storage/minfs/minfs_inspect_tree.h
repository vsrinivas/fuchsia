// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_MINFS_INSPECT_TREE_H_
#define SRC_STORAGE_MINFS_MINFS_INSPECT_TREE_H_

#include <lib/zx/time.h>
#include <zircon/system/public/zircon/compiler.h>

#include <mutex>

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"
#include "src/lib/storage/vfs/cpp/inspect/inspect_tree.h"
#include "src/storage/minfs/format.h"

namespace minfs {

fs_inspect::UsageData CalculateSpaceUsage(const Superblock& superblock, uint64_t reserved_blocks);

// Encapsulates the state required to make a filesystem inspect tree for Minfs.
class MinfsInspectTree final {
 public:
  explicit MinfsInspectTree(const block_client::BlockDevice* device);
  ~MinfsInspectTree() = default;

  // Initialize the Minfs inspect tree, creating all required nodes. Once called, the inspect
  // tree can be queried.
  void Initialize(const fs::FilesystemInfo& fs_info, const Superblock& superblock,
                  uint64_t reserved_blocks) __TA_EXCLUDES(info_mutex_, usage_mutex_);

  // Update resource usage values that change when certain fields in the superblock are modified.
  void UpdateSpaceUsage(const Superblock& superblock, uint64_t reserved_blocks)
      __TA_EXCLUDES(usage_mutex_);

  // Increment the out of space event counter.
  void OnOutOfSpace() __TA_EXCLUDES(volume_mutex_);

  // Increment the recovered space event counter.
  void OnRecoveredSpace() __TA_EXCLUDES(volume_mutex_);

  // Reference to the Inspector this object owns.
  const inspect::Inspector& Inspector() { return inspector_; }

 private:
  // Helper function to create and return all required callbacks to create an fs_inspect tree.
  fs_inspect::NodeCallbacks CreateCallbacks();

  // Update and retrieve latest volume information.
  fs_inspect::VolumeData GetVolumeData() __TA_EXCLUDES(volume_mutex_, device_mutex_);

  mutable std::mutex device_mutex_{};
  const block_client::BlockDevice* const device_ __TA_GUARDED(device_mutex_){};

  //
  // Generic fs_inspect properties
  //

  mutable std::mutex info_mutex_{};
  fs_inspect::InfoData info_ __TA_GUARDED(info_mutex_){};

  mutable std::mutex usage_mutex_{};
  fs_inspect::UsageData usage_ __TA_GUARDED(usage_mutex_){};

  mutable std::mutex volume_mutex_{};
  fs_inspect::VolumeData volume_ __TA_GUARDED(volume_mutex_){};
  uint64_t recovered_space_events_ __TA_GUARDED(volume_mutex_){};

  // Window to limit frequency of reporting for out of space / recovered space events.
  //
  // The properties `out_of_space_events` and `recovered_space_events` answer the following:
  //
  //   1. Has the device attempted to extend the volume but failed within the past 5 minutes?
  //   2. Has the device attempted to extend the volume, and only succeeded after reclaiming
  //      space freed by flushing the journal, in the past 5 minutes?
  //
  // This lets us answer the following questions while being somewhat more robust against user
  // specific workloads (in particular, the amount and rate at which data is written/deleted):
  //   3. How many devices have run out of space in the current boot cycle, at any point in time?
  //   4. When a device does run out of space, does it recover after a certain period of time?
  //      This may allow us to identify patterns over time, e.g. if something temporarily uses a
  //      large amount of space, we might see periodic spikes which then recover for long periods.
  //   5. Has the mitigation added in fxbug.dev/88364 been successful at preventing at least some
  //      out of space issues?
  //
  // These properties may be simplified once we know the answers to #1 and #2 and have more data.
  //
  static constexpr zx::duration kEventWindowDuration = zx::min(5);
  zx::time last_out_of_space_event_ __TA_GUARDED(volume_mutex_){zx::time::infinite_past()};
  zx::time last_recovered_space_event_ __TA_GUARDED(volume_mutex_){zx::time::infinite_past()};

  inspect::LazyNodeCallbackFn CreateDetailNode() const;

  // The Inspector to which the tree is attached.
  inspect::Inspector inspector_;

  // In order to distinguish filesystem instances, we must attach the InspectTree to a uniquely
  // named child node instead of the Inspect root. This is because fshost currently serves all
  // filesystem inspect trees, and is not be required when filesystems are componentized (the tree
  // can be attached directly to the inspect root in that case).
  inspect::Node tree_root_;

  // Filesystem inspect tree nodes.
  // **MUST be declared last**, as the callbacks passed to this object use the above properties.
  // This ensures that the callbacks are destroyed before any properties that they may reference.
  fs_inspect::FilesystemNodes fs_inspect_nodes_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_MINFS_INSPECT_TREE_H_
