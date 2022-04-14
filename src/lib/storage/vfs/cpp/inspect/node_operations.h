// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_INSPECT_NODE_OPERATIONS_H_
#define SRC_LIB_STORAGE_VFS_CPP_INSPECT_NODE_OPERATIONS_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include "src/lib/storage/vfs/cpp/inspect/operation_tracker.h"

namespace fs_inspect {

// Common node operations most filesystems implement.
struct NodeOperations {
  OperationTracker append;
  OperationTracker close;
  OperationTracker create;
  OperationTracker get_attr;
  OperationTracker link;
  OperationTracker lookup;
  OperationTracker read;
  OperationTracker read_dir;
  OperationTracker set_attr;
  OperationTracker sync;
  OperationTracker truncate;
  OperationTracker unlink;
  OperationTracker write;

  explicit NodeOperations(inspect::Node& root_node)
      : append(OperationTracker(root_node, "append")),
        close(OperationTracker(root_node, "close")),
        create(OperationTracker(root_node, "create")),
        get_attr(OperationTracker(root_node, "get_attr")),
        link(OperationTracker(root_node, "link")),
        lookup(OperationTracker(root_node, "lookup")),
        read(OperationTracker(root_node, "read")),
        read_dir(OperationTracker(root_node, "read_dir")),
        set_attr(OperationTracker(root_node, "set_attr")),
        sync(OperationTracker(root_node, "sync")),
        truncate(OperationTracker(root_node, "truncate")),
        unlink(OperationTracker(root_node, "unlink")),
        write(OperationTracker(root_node, "write")) {}
};

}  // namespace fs_inspect

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECT_NODE_OPERATIONS_H_
