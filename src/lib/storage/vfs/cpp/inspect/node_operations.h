// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_INSPECT_NODE_OPERATIONS_H_
#define SRC_LIB_STORAGE_VFS_CPP_INSPECT_NODE_OPERATIONS_H_

#include "src/lib/storage/vfs/cpp/inspect/operation_tracker.h"

namespace fs_inspect {

// Common node operations most filesystems implement.
struct NodeOperations {
  internal::OperationTrackerType close;
  internal::OperationTrackerType read;
  internal::OperationTrackerType write;
  internal::OperationTrackerType append;
  internal::OperationTrackerType truncate;
  internal::OperationTrackerType set_attr;
  internal::OperationTrackerType get_attr;
  internal::OperationTrackerType sync;
  internal::OperationTrackerType read_dir;
  internal::OperationTrackerType lookup;
  internal::OperationTrackerType create;
  internal::OperationTrackerType link;
  internal::OperationTrackerType unlink;

#ifdef __Fuchsia__
  // **WARNING**: The latency histogram settings must match their metric definitions in Cobalt.
  explicit NodeOperations(inspect::Node& node)
      : close(node, "close", kNodeOperationHistogramSettings),
        read(node, "read", kNodeOperationHistogramSettings),
        write(node, "write", kNodeOperationHistogramSettings),
        append(node, "append", kNodeOperationHistogramSettings),
        truncate(node, "truncate", kNodeOperationHistogramSettings),
        set_attr(node, "set_attr", kNodeOperationHistogramSettings),
        get_attr(node, "get_attr", kNodeOperationHistogramSettings),
        sync(node, "sync", kNodeOperationHistogramSettings),
        read_dir(node, "read_dir", kNodeOperationHistogramSettings),
        lookup(node, "lookup", kNodeOperationHistogramSettings),
        create(node, "create", kNodeOperationHistogramSettings),
        link(node, "link", kNodeOperationHistogramSettings),
        unlink(node, "unlink", kNodeOperationHistogramSettings) {}
#else
  // Stub implementation for host builds.
  NodeOperations() = default;
#endif
};

}  // namespace fs_inspect

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECT_NODE_OPERATIONS_H_
