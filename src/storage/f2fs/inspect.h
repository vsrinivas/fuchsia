// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_INSPECT_H_
#define SRC_STORAGE_F2FS_INSPECT_H_

namespace f2fs {

class InspectTree final {
 public:
  explicit InspectTree(F2fs *fs);

  void Initialize();
  void OnOutOfSpace();
  const inspect::Inspector &GetInspector() { return inspector_; }

 private:
  fs_inspect::NodeCallbacks CreateCallbacks();

  F2fs *fs_ = nullptr;

  mutable std::mutex info_mutex_{};
  fs_inspect::InfoData info_ __TA_GUARDED(info_mutex_){};

  mutable std::mutex usage_mutex_{};
  fs_inspect::UsageData usage_ __TA_GUARDED(usage_mutex_){};
  void UpdateUsage() __TA_REQUIRES(usage_mutex_);

  mutable std::mutex fvm_mutex_{};
  fs_inspect::FvmData fvm_ __TA_GUARDED(fvm_mutex_){};
  void UpdateFvmSizeInfo() __TA_REQUIRES(fvm_mutex_);

  // Lasted out of space event within 5 minutes will not be counted.
  static constexpr zx::duration kOutOfSpaceDuration = zx::min(5);
  zx::time last_out_of_space_time_ __TA_GUARDED(fvm_mutex_){zx::time::infinite_past()};

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

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_INSPECT_H_
