// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_INTERNAL_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_INTERNAL_H_

#include <kernel/scheduler.h>

// Implementation of the WAVLTree observer Scheduler::SubtreeMinObserver,
// declared in kernel/scheduler.h. These only need to be visible to the WAVLTree
// methods called in kernel/scheduler.cc. Inclusion elsewhere is superfluous.
//
// The observer maintains an additional invariant per task node in the tree that
// tracks the minimum finish time of all descendent nodes, including the node
// itself. This invariant is the basis of an augmented binary search tree, used
// to find the task with the minimum finish time that also has a start time
// equal or later than the given eligible time.
//
// The augmented search implements the Earliest Eligible Deadline First
// scheduling discipline efficiently in O(log n) time complexity.

// When a node is first inserted into the tree it is a leaf. Set the min finish
// time to the node's own finish time.
template <typename Iter>
void Scheduler::SubtreeMinObserver::RecordInsert(Iter node) {
  node->scheduler_state.min_finish_time_ = node->scheduler_state.finish_time_;
}

// Collisions are not allowed as WAVLTree::insert_or_find is not used by the
// scheduler.
template <typename Iter>
void Scheduler::SubtreeMinObserver::RecordInsertCollision(thread_t* node, Iter collision) {
  ZX_DEBUG_ASSERT_MSG(false, "Key collision: node=%s collision=%s!", node->name, collision->name);
}

// Replacements are not used as WAVLTree::insert_or_replace is not used by the
// scheduler.
template <typename Iter>
void Scheduler::SubtreeMinObserver::RecordInsertReplace(Iter node, thread_t* replacement) {
  ZX_DEBUG_ASSERT_MSG(false, "Key collision: node=%s collision=%s!", node->name, replacement->name);
}

// Adjust each ancestor node as the tree is descended to find the insertion
// point for the new node.
template <typename Iter>
void Scheduler::SubtreeMinObserver::RecordInsertTraverse(thread_t* node, Iter ancestor) {
  if (ancestor->scheduler_state.min_finish_time_ > node->scheduler_state.finish_time_) {
    ancestor->scheduler_state.min_finish_time_ = node->scheduler_state.finish_time_;
  }
}

// Rotations are used to adjust the height of nodes that are out of balance.
// During a rotation, the pivot takes the position of the parent, and takes over
// storing the min finish time for the subtree, as all of the nodes in the
// overall subtree remain the same. The original parent inherits the lr_child
// of the pivot, potentially invalidating its new subtree and requiring an
// update.
//
// The following diagrams the relationship of the nodes in a left rotation:
//
//             pivot                          parent                             |
//            /     \                         /    \                             |
//        parent  rl_child  <-----------  sibling  pivot                         |
//        /    \                                   /   \                         |
//   sibling  lr_child                       lr_child  rl_child                  |
//
// In a right rotation, all of the relationships are reflected. However, this
// does not affect the update logic.
template <typename Iter>
void Scheduler::SubtreeMinObserver::RecordRotation(Iter pivot, Iter lr_child, Iter /*rl_child*/,
                                                   Iter parent, Iter sibling) {
  // The overall subtree maintains the same min.
  pivot->scheduler_state.min_finish_time_ = parent->scheduler_state.min_finish_time_;

  // Compute the min with the newly adopted child.
  parent->scheduler_state.min_finish_time_ = parent->scheduler_state.finish_time_;
  if (sibling) {
    parent->scheduler_state.min_finish_time_ = std::min(parent->scheduler_state.min_finish_time_,
                                                        sibling->scheduler_state.min_finish_time_);
  }
  if (lr_child) {
    parent->scheduler_state.min_finish_time_ = std::min(parent->scheduler_state.min_finish_time_,
                                                        lr_child->scheduler_state.min_finish_time_);
  }
}

// When a node is removed all of the ancestors become invalidated up the the
// root. Traverse up the tree from the point of invalidation and restore the
// subtree invariant.
template <typename Iter>
void Scheduler::SubtreeMinObserver::RecordErase(thread_t* /*node*/, Iter invalidated) {
  Iter current = invalidated;
  while (current) {
    current->scheduler_state.min_finish_time_ = current->scheduler_state.finish_time_;
    if (Iter left = current.left(); left) {
      current->scheduler_state.min_finish_time_ = std::min(
          current->scheduler_state.min_finish_time_, left->scheduler_state.min_finish_time_);
    }
    if (Iter right = current.right(); right) {
      current->scheduler_state.min_finish_time_ = std::min(
          current->scheduler_state.min_finish_time_, right->scheduler_state.min_finish_time_);
    }
    current = current.parent();
  }
}

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_INTERNAL_H_
