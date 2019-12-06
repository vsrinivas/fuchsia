// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_INTRUSIVE_WAVL_TREE_INTERNAL_H_
#define FBL_INTRUSIVE_WAVL_TREE_INTERNAL_H_

#include <stdint.h>

namespace fbl {

namespace tests {
namespace intrusive_containers {
// Fwd decl of sanity checker class used by tests.
class WAVLTreeChecker;

// Definition of the default (no-op) Observer.
//
// Observers are used by the test framework to record the number of insert,
// erase, rank-promote, rank-demote and rotation operations performed during
// usage.  The DefaultWAVLTreeObserver does nothing and should fall out of the
// code during template expansion.
//
// Observers may also be used to maintain additional application-specific per-
// node invariants. For example, maintaining subtree min/max values is useful
// for multikey partition searching.
//
// Note: Records of promotions and demotions are used by tests to demonstrate
// that the computational complexity of insert/erase rebalancing is amortized
// constant.  Promotions and demotions which are side effects of the rotation
// phase of rebalancing are considered to be part of the cost of rotation and
// are not tallied in the overall promote/demote accounting.
//
struct DefaultWAVLTreeObserver {
  // Invoked on the newly inserted node before rebalancing.
  template <typename Iter>
  static void RecordInsert(Iter node) {}

  // Invoked on the node to be inserted and each ancestor node while traversing
  // the tree to find the initial insertion point.
  template <typename T, typename Iter>
  static void RecordInsertTraverse(T* node, Iter ancestor) {}

  // Invoked on the node to be inserted and the colliding node with the same
  // key, during an insert_or_find operation. This method is mutually exclusive
  // with RecordInsertReplace, only one or the other is invoked during an
  // insert operation.
  template <typename T, typename Iter>
  static void RecordInsertCollision(T* node, Iter collision) {}

  // Invoked on an existing node and its replacement, before swapping the
  // replacement into the tree, during an insert_or_replace operation. This
  // method is mutually exclusive with RecordInsertCollision, only one or the
  // other is invoked during an insert operation.
  template <typename Iter, typename T>
  static void RecordInsertReplace(Iter node, T* replacement) {}

  // Invoked after each promotion during post-insert rebalancing.
  static void RecordInsertPromote() {}

  // Invoked after a single rotation during post-insert rebalancing.
  static void RecordInsertRotation() {}

  // Invoked after a double rotation during post-insert rebalancing.
  static void RecordInsertDoubleRotation() {}

  // Invoked on the pivot node, its parent, children, and sibling before a
  // rotation, just before updating the pointers in the relevant nodes. The
  // chirality of the children and sibling is relative to the direction of
  // rotation. The direction of rotation can be determined by comparing these
  // arguments with the values returned by the left() and right() iterator
  // methods of the pivot or parent arguments.
  //
  // The following diagrams the relationship of the nodes in a left rotation:
  //
  //             pivot                          parent                             |
  //            /     \                         /    \                             |
  //        parent  rl_child  <-----------  sibling  pivot                         |
  //        /    \                                   /   \                         |
  //   sibling  lr_child                       lr_child  rl_child                  |
  //
  // In a right rotation, all of the relationships are reflected. However, the
  // left() and right() iterator methods of each node return unreflected values.
  //
  template <typename Iter>
  static void RecordRotation(Iter pivot, Iter lr_child, Iter rl_child, Iter parent,
                             Iter sibling) {}

  // Invoked on the node to be erased and the node in the tree where the
  // augmented invariants become invalid, leading up to the root. Called just
  // after updating the pointers in the relevant nodes, but before rebalancing.
  //
  // The following diagrams the relationship of the erased and invalidated
  // nodes:
  //
  //        root                                                                   |
  //       /    \                                                                  |
  //      A      B    <---- Invalidated starting here on up to the root            |
  //     / \    / \                                                                |
  //    C   D  E   F  <---- Erased node                                            |
  //
  // When the node to be erased has two children, it is first swapped with the
  // leftmost child of the righthand subtree. In this case the invalidated node
  // is the parent of the original leftmost child of the righthand subtree, as
  // this is the deepest node to change after erasure.
  //
  //        root                       root                                        |
  //       /    \                     /    \                                       |
  //      A      B                   A      B                                      |
  //     / \    / \                 / \    / \                                     |
  //    C   D  E   F  <--+         C   D  E   H    <---- Invalidated starting here |
  //              / \    | Swap              / \                                   |
  //             G   H <-+                  G   F  <---- Erased node               |
  //
  template <typename T, typename Iter>
  static void RecordErase(T* node, Iter invalidated) {}

  // Invoked after each demotion during post-erase rebalancing.
  static void RecordEraseDemote() {}

  // Invoked after each single rotation during post-erase rebalancing.
  static void RecordEraseRotation() {}

  // Invoked after each dobule rotation during post-erase rebalancing.
  static void RecordEraseDoubleRotation() {}

  template <typename TreeType>
  static void VerifyRankRule(const TreeType& tree, typename TreeType::RawPtrType node) {}

  template <typename TreeType>
  static void VerifyBalance(const TreeType& tree, uint64_t depth) {}
};

}  // namespace intrusive_containers
}  // namespace tests

using DefaultWAVLTreeRankType = bool;

// Prototypes for the WAVL tree node state.  By default, we just use a bool to
// record the rank parity of a node.  During testing, however, we actually use a
// specialized version of the node state in which the rank is stored as an
// int32_t so that extra sanity checks can be made during balance testing.
//
// Note: All of the data members are stored in the node state base, as are the
// helper methods IsValid and InContainer.  Only the rank manipulation methods
// are in the derived WAVLTreeNodeState class.  This is to ensure that that the
// WAVLTreeNodeState<> struct is a "standard layout type" which allows objects
// which include a WAVLTreeNodeState<> to be standard layout types, provided
// that they follow all of the other the rules as well.
//
template <typename PtrType, typename RankType>  // Fwd decl
struct WAVLTreeNodeStateBase;
template <typename PtrType, typename RankType = DefaultWAVLTreeRankType>  // Partial spec
struct WAVLTreeNodeState;

template <typename PtrType>
struct WAVLTreeNodeState<PtrType, int32_t> : public WAVLTreeNodeStateBase<PtrType, int32_t> {
  bool rank_parity() const { return ((this->rank_ & 0x1) != 0); }
  void promote_rank() { this->rank_ += 1; }
  void double_promote_rank() { this->rank_ += 2; }
  void demote_rank() { this->rank_ -= 1; }
  void double_demote_rank() { this->rank_ -= 2; }
};

}  // namespace fbl

#endif  // FBL_INTRUSIVE_WAVL_TREE_INTERNAL_H_
