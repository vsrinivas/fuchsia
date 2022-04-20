// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_OPERATION_TRACKER_STUB_H_
#define SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_OPERATION_TRACKER_STUB_H_

#include "src/lib/storage/vfs/cpp/inspect/operation_tracker/operation_tracker_base.h"

namespace fs_inspect {

// Stub implementation of OperationTracker for host builds.
class OperationTrackerStub final : public OperationTracker {
 public:
  OperationTrackerStub() = default;

 private:
  void OnSuccess(uint64_t latency_ns) override {}
  void OnError(zx_status_t error) override {}
  void OnError() override {}
};

}  // namespace fs_inspect

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_OPERATION_TRACKER_STUB_H_
