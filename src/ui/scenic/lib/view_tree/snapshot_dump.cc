// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/snapshot_dump.h"

#include <lib/syslog/cpp/macros.h>

#include <stack>
#include <string>

namespace view_tree {

void SnapshotDump::OnNewViewTreeSnapshot(std::shared_ptr<const Snapshot> snapshot) {
  LogDump snapshotLogDump = DumpSnapshotInfo(snapshot);
  if (snapshotLogDump.status == DumpStatus::Failure) {
    FX_LOGS(ERROR) << snapshotLogDump.message << "\n";
  } else {
    FX_LOGS(INFO) << "\n" << snapshotLogDump.message << "\n";
  }
}
// Generates the log string to be dumped using dfs traversal of the view tree
LogDump SnapshotDump::DumpSnapshotInfo(std::shared_ptr<const Snapshot> snapshot) {
  if (snapshot == nullptr) {
    return {"Invalid Snapshot Received", DumpStatus::Failure};
  }
  zx_koid_t root = snapshot->root;
  std::ostringstream dump;

  // Stack takes a pair of the form (zx_koid_t,uint16_t) the former being the Node ID and the latter
  // being the depth of the node
  std::stack<std::pair<zx_koid_t, uint16_t>> stack;
  std::unordered_set<zx_koid_t> visited;
  stack.push({root, 0});
  while (!stack.empty()) {
    auto [node, depth] = stack.top();
    stack.pop();

    // Checks if there is a cycle present in the view tree
    if (visited.count(node) != 0) {
      return {"Cycle Present in the View tree. Invalid Snapshot", DumpStatus::Failure};
    }
    visited.insert(node);
    auto& view_tree = snapshot->view_tree;
    if (view_tree.count(node) == 0) {
      std::ostringstream error_dump;
      error_dump << "Node: " << node << " not present in view_tree. Invalid Request";
      return {error_dump.str(), DumpStatus::Failure};
    }
    for (const auto child : view_tree.at(node).children) {
      stack.push({child, depth + 1});
    }
    Indent(dump, depth);
    DumpNodeInfo(dump, node, view_tree.at(node));
  }
  dump << "Hit testers# : " << snapshot->hit_testers.size() << "\n";
  dump << "Unconnected Views : ";
  std::for_each(snapshot->unconnected_views.begin(), snapshot->unconnected_views.end(),
                [&dump](zx_koid_t view_koid) { dump << view_koid << " "; });
  return {dump.str(), DumpStatus::Success};
}

// Adjusts tabs in the string to represent a tree
void SnapshotDump::Indent(std::ostream& dump, uint16_t depth) {
  while (depth-- != 0)
    dump << " ";
}

// Adds information about the View node which should be displayed on the console.
void SnapshotDump::DumpNodeInfo(std::ostream& dump, zx_koid_t id, const view_tree::ViewNode& node) {
  dump << "|[Node:" << id << "] Parent:" << node.parent << "\n";
}
}  // End namespace view_tree
