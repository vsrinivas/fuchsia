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
  explicit NodeOperations(inspect::Node& root_node)
      : close(OperationTrackerFuchsia(root_node, "close")),
        read(OperationTrackerFuchsia(root_node, "read")),
        write(OperationTrackerFuchsia(root_node, "write")),
        append(OperationTrackerFuchsia(root_node, "append")),
        truncate(OperationTrackerFuchsia(root_node, "truncate")),
        set_attr(OperationTrackerFuchsia(root_node, "set_attr")),
        get_attr(OperationTrackerFuchsia(root_node, "get_attr")),
        sync(OperationTrackerFuchsia(root_node, "sync")),
        read_dir(OperationTrackerFuchsia(root_node, "read_dir")),
        lookup(OperationTrackerFuchsia(root_node, "lookup")),
        create(OperationTrackerFuchsia(root_node, "create")),
        link(OperationTrackerFuchsia(root_node, "link")),
        unlink(OperationTrackerFuchsia(root_node, "unlink")) {}
#else
  // Stub implementation for host builds.
  NodeOperations() = default;
#endif
};

}  // namespace fs_inspect

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECT_NODE_OPERATIONS_H_
