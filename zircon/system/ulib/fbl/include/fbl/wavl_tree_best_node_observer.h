// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef FBL_WAVL_TREE_BEST_NODE_OBSERVER_H_
#define FBL_WAVL_TREE_BEST_NODE_OBSERVER_H_

#include <zircon/assert.h>

#include <fbl/macros.h>

namespace fbl {

// WAVLTreeBestNodeObserver
//
// Definition of a WAVLTreeObserver which helps to automate the process of
// maintaining a "best value in this subtree" invariant inside of a WAVL tree.
//
// For example, consider a set of objects, each of which has an "Priority" and
// an "Awesomeness" property.  If a user is maintaining a collection of these
// objects indexed by "Priority", they may want to be able to easily answer
// questions about the maximum "Awesomeness" of sets of objects partitioned by
// priority.  If every member in the tree also maintained a "maximum Awesomeness
// for my subtree" value, then the following questions can be easily answered.
//
// 1) What is the maximum awesomeness across all members of the tree?  This is
//    the maximum subtree awesomeness of the root node of the tree.
// 2) What is the maximum awesomeness across all members of the tree with a
//    priority > X?  This is the maximum subtree awesomeness of
//    tree.upper_bound(X) (if such a node exists).
//
// WAVLTreeBestNodeObserver implements all of the observer hooks needed to
// maintain such an invariant based on a set of Traits defined by the user which
// allow the WAVLTreeBestNodeObserver to know when one node has a "better" value
// than another node, and to access the per-node storage which holds the "best"
// value for a subtree.
//
// Traits implementations should contain the following definitions:
//
// struct Traits {
//  // TODO(johngro): When we are allowed to use C++20, make this a more formal
//  // concept definition.
//  // Returns a node's value.  In the example above, this is the node's "awesomeness".
//  static ValueType GetValue(const Object& node) { ... }
//
//  // Returns the current "best" value of the subtree rooted at node.
//  static ValueType GetSubtreeBest(const Object& node) { ... }
//
//  // Compares two values, and returns true if |a| is "better" than |b|.  Otherwise false.
//  static bool Compare(ValueType a, ValueType b) { ... }
//
//  // Assigns the value |val| to the node's subtree-best storage.
//  static void AssignBest(Object& node, ValueType val) { ... }
//
//  // Resets the value node's subtree-best storage.  Called when nodes are
//  // being removed from their tree.  Note that users don't _have_ to make use
//  // of this hook if they do not care about stale values being stored in their
//  // subtree-best storage.
//  static void ResetBest(Object& target) { ... }
// };
//
// The Traits used for the "awesomness" example given above might look like the
// following if "awesomeness" was expressed as a strictly positive uint32_t:
//
// struct AwesomeObj {
//   // ...
//   constexpr uint32_t kInvalidAwesomeness = 0;
//   uint32_t priority;
//   uint32_t awesomeness;
//   uint32_t subtree_best{kInvalidAwesomeness};
// };
//
// struct MaxAwesomeTraits {
//  static uint32_t GetValue(const AwesomeObj& node) { return node.awesomness; }
//  static uint32_t GetSubtreeBest(const AwesomeObj& node) { return node.subtree_best; }
//  static bool Compare(uint32_t a, uint32_t b) { return a > b; }
//  static void AssignBest(AwesomeObj& node, uint32_t val) { node.subtree_best = val; }
//  static void ResetBest(AwesomeObj& target) {
//    node.subtree_best = AwesomeObj::kInvalidAwesomeness;
//  }
// };
//
// using MaxAwesomeObserver = fbl::WAVLReeBestNodeObserver<MaxAwesomeTraits>;
//
// In addition to the traits which define the "best" value to maintain,
// WAVLTreeBestNodeObserver has two more boolean template parameters which can
// be used to control behavior.  They are:
//
// AllowInsertOrFindCollision
// AllowInsertOrReplaceCollision
//
// By default, both of these values are |true|.  If a collision happens during
// either an insert_or_find or an insert_or_replace operation, the "best value"
// invariant will be maintained.  On the other hand, if a user knows that they
// will never encounter collisions as a result of one or the other (or both) of
// these operations, they may set the appropriate Allow template argument to
// false, causing the WAVLTreeBestNodeObserver to fail a ZX_DEBUG_ASSERT in the
// case that associated collision is ever encountered during operation.
//
template <typename Traits, bool AllowInsertOrFindCollision = true,
          bool AllowInsertOrReplaceCollision = true>
struct WAVLTreeBestNodeObserver {
 private:
  DECLARE_HAS_MEMBER_FN(has_on_insert_collision, OnInsertCollision);
  DECLARE_HAS_MEMBER_FN(has_on_insert_replace, OnInsertReplace);

 public:
  template <typename Iter>
  static void RecordInsert(Iter node) {
    Traits::AssignBest(*node, Traits::GetValue(*node));
  }

  template <typename T, typename Iter>
  static void RecordInsertCollision(T* node, Iter collision) {
    // |node| did not actually end up getting inserted into the tree, but all of
    // the node down until collision we updated during the descent during calls
    // to RecordInsertTraverse.  We need to restore the "best" invariant by
    // re-computing the proper values starting from collision, up until we reach
    // root.
    ZX_DEBUG_ASSERT(AllowInsertOrFindCollision);
    RecomputeUntilRoot(collision);
  }

  template <typename Iter, typename T>
  static void RecordInsertReplace(Iter node, T* replacement) {
    // |node| is still in the tree, but it is about to be replaced by
    // |replacement|.  Update the value of |node| to hold the value of
    // |replacement|, then propagate the value up the tree to the root (as we do
    // in the case of an erase operation).  Once  we are finished, transfer the
    // computed "best" value for for |node|'s subtree over to replacement, then
    // reset the value of node (as it is just about to be removed from the tree,
    // and replaced with |replacement|).
    ZX_DEBUG_ASSERT(AllowInsertOrReplaceCollision);
    UpdateBest(Traits::GetValue(*replacement), node);
    RecomputeUntilRoot(node.parent());
    Traits::AssignBest(*replacement, Traits::GetSubtreeBest(*node));
    Traits::ResetBest(*node);
  }

  template <typename T, typename Iter>
  static void RecordInsertTraverse(T* node, Iter ancestor) {
    // If the value of |node| is better than the value of the ancestor we just
    // traversed, update the |ancestor| to hold |node|'s value.
    const auto node_val = Traits::GetValue(*node);
    if (Traits::Compare(node_val, Traits::GetSubtreeBest(*ancestor))) {
      Traits::AssignBest(*ancestor, node_val);
    }
  }

  // Rotations are used to adjust the height of nodes that are out of balance.
  // During a rotation, the pivot takes the position of the parent, and takes over
  // storing the "best" value for the subtree, as all of the nodes in the
  // overall subtree remain the same. The original parent inherits the lr_child
  // of the pivot, potentially invalidating its new subtree and requiring an
  // update.
  //
  // The following diagrams the relationship of the nodes in a left rotation:
  //
  //           ::After::                      ::Before::                           |
  //                                                                               |
  //             pivot                          parent                             |
  //            /     \                         /    \                             |
  //        parent  rl_child  <-----------  sibling  pivot                         |
  //        /    \                                   /   \                         |
  //   sibling  lr_child                       lr_child  rl_child                  |
  //
  // In a right rotation, all of the relationships are reflected. However, this
  // does not affect the update logic.
  template <typename Iter>
  static void RecordRotation(Iter pivot, Iter lr_child, Iter rl_child, Iter parent, Iter sibling) {
    // |pivot| is about to take |parent|'s place in the tree.  The overall
    // subtree maintains the same "best" value, so |pivot| can just take
    // |parent|'s best value..
    Traits::AssignBest(*pivot, Traits::GetSubtreeBest(*parent));

    // The descendents of |sibling|, |lr_child|, and |rl_child| are not
    // changing, meaning that we do not need to take any action to update their
    // current best subtree values.
    //
    // |parent|, on the other hand, is becoming the root of a new subtree with
    // |sibling| and |lr_child| as its new children.  Select the new best value
    // for the subtree rooted at |parent| from these three options.
    //
    auto best = Traits::GetValue(*parent);

    if (sibling) {
      const auto sibling_best = Traits::GetSubtreeBest(*sibling);
      if (Traits::Compare(sibling_best, best)) {
        best = sibling_best;
      }
    }

    if (lr_child) {
      const auto lr_child_best = Traits::GetSubtreeBest(*lr_child);
      if (Traits::Compare(lr_child_best, best)) {
        best = lr_child_best;
      }
    }

    Traits::AssignBest(*parent, best);
  }

  template <typename T, typename Iter>
  static void RecordErase(T* node, Iter invalidated) {
    // When a node is removed all of the ancestors become invalidated up to
    // the root. Traverse up the tree from the point of invalidation and
    // restore the subtree invariant.  Note, there will be no invalidated node
    // in the case that this was the last node removed.
    RecomputeUntilRoot(invalidated);

    // |node| is leaving the tree.  Give our Traits the opportunity to reset
    // its "best" value.
    Traits::ResetBest(*node);
  }

  // Promotion/Demotion/DoubleRotation count hooks are not needed to maintain
  // our "best" invariant.
  static void RecordInsertPromote() {}
  static void RecordInsertRotation() {}
  static void RecordInsertDoubleRotation() {}
  static void RecordEraseDemote() {}
  static void RecordEraseRotation() {}
  static void RecordEraseDoubleRotation() {}

 private:
  template <typename Iter>
  static void RecomputeUntilRoot(Iter current) {
    for (; current; current = current.parent()) {
      UpdateBest(Traits::GetValue(*current), current);
    }
  }

  template <typename ValueType, typename Iter>
  static void UpdateBest(ValueType value, Iter node) {
    if (Iter left = node.left(); left) {
      ValueType left_best = Traits::GetSubtreeBest(*left);
      if (Traits::Compare(left_best, value)) {
        value = left_best;
      }
    }

    if (Iter right = node.right(); right) {
      ValueType right_best = Traits::GetSubtreeBest(*right);
      if (Traits::Compare(right_best, value)) {
        value = right_best;
      }
    }

    Traits::AssignBest(*node, value);
  }
};

}  // namespace fbl

#endif  // FBL_WAVL_TREE_BEST_NODE_OBSERVER_H_
