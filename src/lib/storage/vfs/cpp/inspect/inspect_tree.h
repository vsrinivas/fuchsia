// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// This file contains functions/types that filesystems can be used to create a standardized inspect
// tree. This is done by invoking `CreateTree` with callbacks that return the filesystem's inspect
// data (see `NodeCallbacks`). `CreateTree` creates and returns ownership of inspect nodes which map
// the data from these callbacks to inspect properties.
//
// See README.md for more details. For an example, see the Blobfs implementation in
// `src/storage/blobfs/blobfs_inspect_tree.h`.
//

#ifndef SRC_LIB_STORAGE_VFS_CPP_INSPECT_INSPECT_TREE_H_
#define SRC_LIB_STORAGE_VFS_CPP_INSPECT_INSPECT_TREE_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/result.h>

#include <functional>
#include <memory>

#include "src/lib/storage/vfs/cpp/inspect/inspect_data.h"

namespace fs_inspect {

// Node Names

constexpr char kInfoNodeName[] = "fs.info";
constexpr char kUsageNodeName[] = "fs.usage";
constexpr char kFvmNodeName[] = "fs.fvm";
constexpr char kVolumesNodeName[] = "fs.volumes";
constexpr char kDetailNodeName[] = "fs.detail";

// Callbacks that a filesystem must provide to expose a standard inspect hierarchy.
// The callbacks will be invoked asynchronously each time the inspect tree is snapshotted.
// The data types referenced the callbacks below are defined in `inspect_data.h`.
struct NodeCallbacks {
  // Callback invoked when populating the fs.info node. Must not be nullptr.
  std::function<InfoData()> info_callback;
  // Callback invoked when populating the fs.usage node. Must not be nullptr.
  std::function<UsageData()> usage_callback;
  // Callback invoked when populating the fs.fvm node. Must not be nullptr.
  std::function<FvmData()> fvm_callback;
  // Callback which creates the LazyNode for fs.detail. If nullptr, fs.detail will not be created.
  inspect::LazyNodeCallbackFn detail_node_callback = nullptr;
};

// Maintains ownership of the inspect nodes as well as their respective callbacks.
//
// Can be created by calling `Attach`.
struct FilesystemNodes {
  inspect::LazyNode info;
  inspect::LazyNode usage;
  inspect::LazyNode fvm;
  inspect::LazyNode detail;
};

// Create and returns ownership of standard filesystem inspect tree nodes, attaching them under the
// given `root` node.
//
// The callbacks provided in `node_callbacks` may be invoked asynchronously until the returned
// `FilesystemNodes` object is destroyed.
FilesystemNodes CreateTree(inspect::Node& root, NodeCallbacks node_callbacks);

}  // namespace fs_inspect

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECT_INSPECT_TREE_H_
