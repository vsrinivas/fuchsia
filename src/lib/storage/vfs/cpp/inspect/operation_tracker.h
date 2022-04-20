// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_H_
#define SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_H_

#include <lib/zx/time.h>
#include <zircon/status.h>

#include <functional>
#include <optional>

//
// Provides tracking of various filesystem operations, including stubs for host builds.
//

#include "src/lib/storage/vfs/cpp/inspect/operation_tracker/operation_tracker_base.h"

#ifdef __Fuchsia__
#include "src/lib/storage/vfs/cpp/inspect/operation_tracker/operation_tracker_fuchsia.h"
#else
#include "src/lib/storage/vfs/cpp/inspect/operation_tracker/operation_tracker_stub.h"
#endif
namespace fs_inspect::internal {
#ifdef __Fuchsia__
using OperationTrackerType = OperationTrackerFuchsia;
#else
using OperationTrackerType = OperationTrackerStub;
#endif
}  // namespace fs_inspect::internal

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECT_OPERATION_TRACKER_H_
