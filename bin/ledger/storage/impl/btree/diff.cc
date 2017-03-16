// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/diff.h"

#include "apps/ledger/src/storage/impl/btree/internal_helper.h"
#include "apps/ledger/src/storage/impl/btree/iterator.h"
#include "apps/ledger/src/storage/impl/btree/synchronous_storage.h"

namespace storage {
namespace btree {
namespace {

// Aggregates 2 BTreeIterator and allows to walk these concurrently to compute
// the diff.
class IteratorPair {
 public:
  IteratorPair(SynchronousStorage* storage,
               const std::function<bool(EntryChange)>& on_next)
      : on_next_(on_next), left_(storage), right_(storage) {}

  // Initialize the pair with the ids of both roots.
  Status Init(ObjectIdView left_node_id, ObjectIdView right_node_id) {
    Status status;
    status = left_.Init(left_node_id);
    if (status == Status::OK) {
      status = right_.Init(right_node_id);
    }
    if (status == Status::OK) {
      Normalize();
    }
    if (status == Status::OK && !Finished() && !HasDiff()) {
      status = Advance();
    }

    return status;
  }

  bool Finished() {
    FTL_DCHECK(IsNormalized());
    return right_.Finished();
  }

  // Send the actual diff to the client. Returns |false| if the iteration must
  // be stopped.
  bool SendDiff() {
    FTL_DCHECK(HasDiff());

    // If the 2 iterators are on 2 equals values, nothing to do.
    if (left_.HasValue() && right_.HasValue() &&
        left_.CurrentEntry() == right_.CurrentEntry()) {
      return true;
    }

    if (HasSameNextChild()) {
      // If the 2 iterators are on the same child, send a diff for each
      // iterator that is currently on a value.
      if (right_.HasValue()) {
        if (!SendRight()) {
          return false;
        }
      }
      if (left_.HasValue() &&
          (!right_.HasValue() ||
           left_.CurrentEntry().key != right_.CurrentEntry().key)) {
        if (!SendLeft()) {
          return false;
        }
      }
      return true;
    }

    // Otherwise, just send the diff of the right node.
    return SendRight();
  }

  // Advance the iterator until there is potentially a diff to send.
  Status Advance() {
    FTL_DCHECK(!Finished());
    do {
      FTL_DCHECK(IsNormalized());

      // If the 2 next children are identical, skip these.
      if (HasSameNextChild()) {
        right_.SkipNextSubTree();
        left_.SkipNextSubTree();
        Normalize();
        continue;
      }

      // If both iterators are sitting on a value for the same key, both need
      // to be advanced.
      if (right_.HasValue() && left_.HasValue() &&
          right_.CurrentEntry().key == left_.CurrentEntry().key) {
        Status status = right_.Advance();
        if (status != Status::OK) {
          return status;
        }
        Swap();
      }

      Status status = right_.Advance();
      if (status != Status::OK) {
        return status;
      }
      Normalize();
    } while (!Finished() && !HasDiff());
    return Status::OK;
  }

 private:
  // Ensure that the representation of the pair of iterator is normalized
  // according to the following rules:
  // If only one iterator is finished, it is always the left one.
  // If only one iterator is on a value, it is always the left one.
  // If both iterator are on a value, the left one has a key greater or equals
  //     to the right one, and if the keys are equals, the iterator are in
  //     their original order.
  // If none of the iterators is on a value, the right one has a level
  //     greather or equals to the left one.
  //
  // When the iterator is normalized, the different algorithms can cut the
  // number of case it needs to consider.
  void Normalize() {
    if (left_.Finished()) {
      return;
    }
    if (right_.Finished()) {
      Swap();
      return;
    }

    if (right_.HasValue() && left_.HasValue()) {
      if (left_.CurrentEntry().key < right_.CurrentEntry().key) {
        Swap();
        return;
      }
      if (left_.CurrentEntry().key == right_.CurrentEntry().key) {
        ResetSwap();
      }
      return;
    }

    if (left_.HasValue()) {
      return;
    }
    if (right_.HasValue()) {
      Swap();
      return;
    }

    if (left_.GetLevel() > right_.GetLevel()) {
      Swap();
    }
  }

  // Returns if the iterator is normalized. See |Normalize| for the
  // definition. This is only used in DCHECKs.
  bool IsNormalized() const {
    if (left_.Finished() || right_.Finished()) {
      return left_.Finished();
    }

    if (left_.HasValue()) {
      if (!right_.HasValue()) {
        return true;
      }

      if (right_.CurrentEntry().key > left_.CurrentEntry().key) {
        return false;
      }

      if (right_.CurrentEntry().key == left_.CurrentEntry().key) {
        if (!diff_from_left_to_right_)
          return diff_from_left_to_right_;
      }

      return true;
    }

    if (right_.HasValue()) {
      return false;
    }

    return right_.GetLevel() >= left_.GetLevel();
  }

  // Returns whether there is a potential diff to send at the current state.
  bool HasDiff() const {
    FTL_DCHECK(IsNormalized());
    return (right_.HasValue() && (left_.Finished() || left_.HasValue())) ||
           (left_.HasValue() && HasSameNextChild());
  }

  // Returns whether the 2 iterators have the same next child in the
  // iteration. This allows to skip part of the 2 btrees when they are
  // identicals.
  bool HasSameNextChild() const {
    return !left_.Finished() && !right_.GetNextChild().empty() &&
           right_.GetNextChild() == left_.GetNextChild();
  }

  // Swaps the 2 iterators. This is useful to reduce the number of case to
  // consider during the iteration.
  void Swap() {
    std::swap(left_, right_);
    diff_from_left_to_right_ = !diff_from_left_to_right_;
  }

  // Reset the iterators so that they are back in the original order.
  void ResetSwap() {
    if (!diff_from_left_to_right_) {
      Swap();
    }
  }

  // Send a diff using the right iterator.
  bool SendRight() {
    return on_next_({right_.CurrentEntry(), !diff_from_left_to_right_});
  }

  // Send a diff using the left iterator.
  bool SendLeft() {
    return on_next_({left_.CurrentEntry(), diff_from_left_to_right_});
  }

  const std::function<bool(EntryChange)>& on_next_;
  BTreeIterator left_;
  BTreeIterator right_;
  // Keep track whether the change is from left to right, or right to left.
  // This allows to switch left and right during the algorithm to handle less
  // cases.
  bool diff_from_left_to_right_ = true;
};

Status ForEachDiffInternal(SynchronousStorage* storage,
                           ObjectIdView left_node_id,
                           ObjectIdView right_node_id,
                           const std::function<bool(EntryChange)>& on_next) {
  if (left_node_id == right_node_id) {
    return Status::OK;
  }

  IteratorPair iterators(storage, on_next);
  Status status = iterators.Init(left_node_id, right_node_id);
  if (status != Status::OK) {
    return status;
  }

  while (!iterators.Finished()) {
    if (!iterators.SendDiff()) {
      return Status::OK;
    }
    Status status = iterators.Advance();
    if (status != Status::OK) {
      return status;
    }
  }

  return Status::OK;
}

}  // namespace

void ForEachDiff(coroutine::CoroutineService* coroutine_service,
                 PageStorage* page_storage,
                 ObjectIdView base_root_id,
                 ObjectIdView other_root_id,
                 std::function<bool(EntryChange)> on_next,
                 std::function<void(Status)> on_done) {
  coroutine_service->StartCoroutine([
    page_storage, base_root_id, other_root_id, on_next = std::move(on_next),
    on_done = std::move(on_done)
  ](coroutine::CoroutineHandler * handler) {
    SynchronousStorage storage(page_storage, handler);

    on_done(
        ForEachDiffInternal(&storage, base_root_id, other_root_id, on_next));
  });
}

}  // namespace btree
}  // namespace storage
