// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_VIEW_TREE_SNAPSHOT_DUMP_H_
#define SRC_UI_SCENIC_LIB_VIEW_TREE_SNAPSHOT_DUMP_H_

#include <sstream>

#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace view_tree {

enum class DumpStatus {
  Success = 0,
  Failure,
};

struct LogDump {
  std::string message;
  DumpStatus status;
};

// Snapshot dump helps in dumping out the necessary information of the snapshot view tree
// to the console. This information gets printed whenever a new frame is loaded.
// Eg:- if the view tree is of the form
//  A(1)
//  |
//  B(2)
// The dump printed would be
// |[Node 1] Parent 0
//         |[Node 2] Parent 1
// Hit tester#: ${No of hit testers}
// Unconnected views: ${space separated koids of view nodes}
class SnapshotDump {
 public:
  static void OnNewViewTreeSnapshot(std::shared_ptr<const Snapshot> snapshot);
  static LogDump DumpSnapshotInfo(std::shared_ptr<const Snapshot> snapshot);

 private:
  static void Indent(std::ostream& dump, uint16_t depth);
  static void DumpNodeInfo(std::ostream& dump, zx_koid_t id, const view_tree::ViewNode& node);
};
}  // namespace view_tree
#endif  // SRC_UI_SCENIC_LIB_VIEW_TREE_SNAPSHOT_DUMP_H_
