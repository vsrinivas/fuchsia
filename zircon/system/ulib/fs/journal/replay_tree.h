// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_FS_JOURNAL_REPLAY_TREE_H_
#define ZIRCON_SYSTEM_ULIB_FS_JOURNAL_REPLAY_TREE_H_

#include <fs/transaction/writeback.h>
#include <range/interval-tree.h>
#include <range/range.h>
#include <storage/operation/buffered_operation.h>

namespace fs {
namespace internal {

// Container for BufferedOperations collected during replay.
//
// The "dev_offset" is used as a key for determining overlap.
class RangeContainer {
 public:
  explicit RangeContainer(storage::BufferedOperation op) : operation(std::move(op)) {}
  uint64_t Start() const { return operation.op.dev_offset; }
  uint64_t End() const { return operation.op.dev_offset + operation.op.length; }
  void Update(uint64_t start, uint64_t end) {
    // update is called during range merges and splits. during these operations, vmo_offset stays a
    // constant distance away from dev_offset. calculate the movement of dev_offset in this
    // operation and move vmo_offset accordingly.
    int64_t diff = static_cast<int64_t>(start) - static_cast<int64_t>(operation.op.dev_offset);
    operation.op.vmo_offset += diff;
    operation.op.dev_offset = start;
    operation.op.length = end - start;
  }

  storage::BufferedOperation operation;
};

// Traits which enable a BufferedOperation to exist in an interval tree.
struct RangeTraits {
  static uint64_t Start(const RangeContainer& obj) { return obj.Start(); }
  static uint64_t End(const RangeContainer& obj) { return obj.End(); }
  static zx_status_t Update(const RangeContainer* other, uint64_t start, uint64_t end,
                            RangeContainer* obj);
};

using BufferedOperationRange = range::Range<uint64_t, RangeContainer, RangeTraits>;
using BufferedOperationTree = range::IntervalTree<BufferedOperationRange>;

}  // namespace internal

// A tree which enables a caller to collect BufferedOperation objects
// used during journal replay. On insertion, the tree is updated
// to use the "latest" operation targetting a particular block.
class ReplayTree {
 public:
  using IterType = typename internal::BufferedOperationTree::IterType;
  using ConstIterType = typename internal::BufferedOperationTree::ConstIterType;

  ReplayTree();

  // Inserts an operation into the replay tree.
  //
  // First, removes all overlapping prior operations which target the same
  // device offset, and then inserts |operation|. This ensures that the "latest
  // operation touching block B" will be the only operation from replay targetting
  // that block.
  void insert(storage::BufferedOperation operation);

  void clear() { tree_.clear(); }

  bool empty() const { return tree_.empty(); }

  size_t size() const { return tree_.size(); }

  IterType begin() { return tree_.begin(); }

  ConstIterType begin() const { return tree_.begin(); }

  IterType end() { return tree_.end(); }

  ConstIterType end() const { return tree_.end(); }

 private:
  internal::BufferedOperationTree tree_;
};

}  // namespace fs

#endif  // ZIRCON_SYSTEM_ULIB_FS_JOURNAL_REPLAY_TREE_H_
