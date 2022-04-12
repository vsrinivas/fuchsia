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
  OperationTracker close;
  OperationTracker read;
  OperationTracker write;
  OperationTracker append;
  OperationTracker truncate;
  OperationTracker set_attr;
  OperationTracker get_attr;
  OperationTracker sync;
  OperationTracker read_dir;
  OperationTracker look_up;
  OperationTracker create;
  OperationTracker link;
  OperationTracker unlink;

  explicit NodeOperations(inspect::Node& root_node)
      : close(OperationTracker(root_node, "close")),
        read(OperationTracker(root_node, "read")),
        write(OperationTracker(root_node, "write")),
        append(OperationTracker(root_node, "append")),
        truncate(OperationTracker(root_node, "truncate")),
        set_attr(OperationTracker(root_node, "set_attr")),
        get_attr(OperationTracker(root_node, "get_attr")),
        sync(OperationTracker(root_node, "sync")),
        read_dir(OperationTracker(root_node, "read_dir")),
        look_up(OperationTracker(root_node, "look_up")),
        create(OperationTracker(root_node, "create")),
        link(OperationTracker(root_node, "link")),
        unlink(OperationTracker(root_node, "unlink")) {}
};

}  // namespace fs_inspect

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECT_NODE_OPERATIONS_H_
