// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/btree/btree_iterator.h"

#include "apps/ledger/storage/impl/store/tree_node.h"
#include "lib/ftl/logging.h"

namespace storage {

BTreeIterator::BTreeIterator(std::unique_ptr<const TreeNode> root) {
  // Initialize the iterator.
  std::unique_ptr<const TreeNode> current_node = std::move(root);
  while (current_node) {
    std::unique_ptr<const TreeNode> next_node;
    Status status = current_node->GetChild(0, &next_node);
    // We position the current reading pointer just "before" the beginning of
    // the node.
    stack_.emplace(std::move(current_node), -1, 0);
    if (status == Status::OK) {
      current_node = std::move(next_node);
    } else if (status == Status::NOT_FOUND) {
      break;
    } else {
      current_status_ = status;
    }
  }
  if (Valid()) {
    this->Next();
  }
}

BTreeIterator::~BTreeIterator() {}

BTreeIterator& BTreeIterator::Seek(const std::string& key) {
  if (!Valid()) {
    return *this;
  }

  if (key < (*this)->key) {
    return *this;
  }

  std::unique_ptr<const TreeNode> current_node;
  // Clear the stack.
  while (!stack_.empty()) {
    current_node.swap(stack_.top().node);
    stack_.pop();
  }
  while (current_node) {
    int index;
    Status status = current_node->FindKeyOrChild(key, &index);
    if (status == Status::OK) {
      stack_.emplace(std::move(current_node), index, index);
      stack_.top().node->GetEntry(stack_.top().entry_index, &current_entry_);
    } else if (status == Status::NOT_FOUND) {
      std::unique_ptr<const TreeNode> next_node;
      Status child_status = current_node->GetChild(index, &next_node);
      stack_.emplace(std::move(current_node), index - 1, index);
      if (child_status == Status::OK) {
        current_node = std::move(next_node);
        // If child_status != Status::OK, then current_node would remain unset
        // after the move above, then we will exit the loop.
      } else if (child_status == Status::NOT_FOUND) {
        Next();
      } else {
        current_status_ = child_status;
      }
    } else {
      current_status_ = status;
      break;
    }
  }
  return *this;
}

BTreeIterator& BTreeIterator::Next() {
  FTL_DCHECK(Valid());
  // direction_up tells the algorithm whether we are exploring the tree down
  // (from root to leaves) or up (from leaves to root). In the down direction,
  // we are exploring the children of each node: when exploring a node, we look
  // for its next unexplored child and explore it, recursively, until we reach
  // the bottom of the tree. In the up direction, we are exploring the entries
  // of each node: starting at the bottom, we look at the next entry of the
  // explored node; if there is no such entry (we are at the end), we go to the
  // next node up the stack and look at its next entry, and again, until we
  // either find a valid entry or we unrolled the whole tree.
  // The normal mode of operation when nodes are not empty is to explore "down"
  // [3] until no child is found [4], then go "up" and return the next entry of
  // the bottom node [1]. If we are also at the end of the entry list, we go up
  // further [2].
  bool direction_up = false;
  while (!stack_.empty()) {
    Position* current_position = &stack_.top();
    if (direction_up) {
      // We are moving up in the tree, as we finished exploring the previous
      // branch. We now point to the next entry (key/value pair).
      current_position->entry_index++;
      if (current_position->entry_index <
          current_position->node->GetKeyCount()) {
        // [1] There is a next entry in this node, point to it and exit.
        current_position->node->GetEntry(current_position->entry_index,
                                         &current_entry_);
        break;
      }
      // [2] We are at the end of this node, so let's continue to move up the
      // tree.
      stack_.pop();
      continue;
    }

    // We are either at the beginning of a node, or already explored its entry
    // at entry_index, so we explore the next child of the node.
    current_position->child_index++;

    if (current_position->child_index <
        current_position->node->GetKeyCount() + 1) {
      // We are not at the end of the child list, given the number of
      // entries stored in this node. However, this doesn't mean that this
      // child won't be empty.
      std::unique_ptr<const TreeNode> next_child;
      Status status = current_position->node->GetChild(
          current_position->child_index, &next_child);

      if (status == Status::OK) {
        // [3] The child is not empty, so we add it to the stack to explore it.
        stack_.emplace(std::move(next_child), -1, -1);
      } else if (status == Status::NOT_FOUND) {
        // [4] The child is empty, so we reverse direction. This will lead us to
        // try to read the next entry, if available.
        direction_up = true;
      } else {
        current_status_ = status;
        return *this;
      }
      continue;
    }
    // [5] We are at the end of the child list, so this node is completely
    // explored now. Remove it from the stack and go up.
    direction_up = true;
    stack_.pop();
  }
  return *this;
}

bool BTreeIterator::Valid() const {
  return !stack_.empty() && current_status_ == Status::OK;
}

Status BTreeIterator::GetStatus() const {
  return current_status_;
}

const Entry& BTreeIterator::operator*() const {
  FTL_DCHECK(Valid());
  return current_entry_;
}

const Entry* BTreeIterator::operator->() const {
  FTL_DCHECK(Valid());
  return &current_entry_;
}

}  // namespace storage
