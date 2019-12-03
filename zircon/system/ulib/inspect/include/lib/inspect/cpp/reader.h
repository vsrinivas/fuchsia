// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_READER_H_
#define LIB_INSPECT_CPP_READER_H_

#include <lib/fit/promise.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/snapshot.h>

namespace inspect {

// Construct a new Hierarchy by synchronously reading nodes out of
// the given VMO.
fit::result<Hierarchy> ReadFromVmo(const zx::vmo& vmo);

// Construct a new Hierarchy by synchronously reading nodes out of the
// given VMO Snapshot.
fit::result<Hierarchy> ReadFromSnapshot(Snapshot snapshot);

// Construct a new Hierarchy by synchronously reading nodes out of the
// contents of the given buffer.
fit::result<Hierarchy> ReadFromBuffer(std::vector<uint8_t> buffer);

// Construct a new Hierarchy by reading nodes out of the given Inspector, including
// all linked hierarchies.
fit::promise<Hierarchy> ReadFromInspector(Inspector insp);

namespace internal {
// Keeps track of a particular snapshot and the snapshots that are linked off of it.
struct SnapshotTree {
  // The snapshot of a VMO at a point in the tree.
  Snapshot snapshot;

  // Map from name to the SnapshotTree for a child of this snapshot.
  std::map<std::string, SnapshotTree> children;
};

// Parses a tree of snapshots into its corresponding Hierarchy, following all links.
fit::result<Hierarchy> ReadFromSnapshotTree(const SnapshotTree& tree);
}  // namespace internal

}  // namespace inspect

#endif  // LIB_INSPECT_CPP_READER_H_
