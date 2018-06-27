// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/btree/diff.h"

#include <utility>

#include <lib/fit/function.h>

#include "peridot/bin/ledger/storage/impl/btree/internal_helper.h"
#include "peridot/bin/ledger/storage/impl/btree/iterator.h"
#include "peridot/bin/ledger/storage/impl/btree/synchronous_storage.h"
#include "peridot/bin/ledger/storage/impl/object_digest.h"

namespace storage {
namespace btree {
namespace {
// Aggregates 2 |BTreeIterator|s and allows to walk through these concurrently
// to compute the diff. |on_next| will be called for each diff entry.
class IteratorPair {
 public:
  IteratorPair(
      SynchronousStorage* storage,
      fit::function<bool(std::unique_ptr<Entry>, std::unique_ptr<Entry>)>
          on_next)
      : on_next_(std::move(on_next)), left_(storage), right_(storage) {}

  // Initialize the pair with the ids of both roots.
  Status Init(ObjectIdentifier left_node_identifier,
              ObjectIdentifier right_node_identifier, fxl::StringView min_key) {
    RETURN_ON_ERROR(left_.Init(left_node_identifier));
    RETURN_ON_ERROR(right_.Init(right_node_identifier));
    if (!min_key.empty()) {
      RETURN_ON_ERROR(SkipIteratorsTo(min_key));
    }
    Normalize();
    if (!Finished() && !HasDiff()) {
      RETURN_ON_ERROR(Advance());
    }

    return Status::OK;
  }

  bool Finished() const {
    FXL_DCHECK(IsNormalized());
    return right_.Finished();
  }

  // Send the actual diff to the client. Returns |false| if the iteration must
  // be stopped.
  bool SendDiff() {
    FXL_DCHECK(HasDiff());

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
    FXL_DCHECK(!Finished());
    do {
      FXL_DCHECK(IsNormalized());

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
        RETURN_ON_ERROR(right_.Advance());
        Swap();
      }

      RETURN_ON_ERROR(right_.Advance());
      Normalize();
    } while (!Finished() && !HasDiff());
    return Status::OK;
  }

 private:
  // Advances the two iterators so that they are both in the first entry that 1)
  // is greater than or equal to min_key and 2) might be different between the
  // two iterators. We consider that the two entries might be different, if they
  // are in btree nodes with different ids.
  Status SkipIteratorsTo(fxl::StringView min_key) {
    for (;;) {
      if (left_.SkipToIndex(min_key)) {
        right_.SkipTo(min_key);
        return Status::OK;
      }
      if (right_.SkipToIndex(min_key)) {
        left_.SkipToIndex(min_key);
        return Status::OK;
      }

      auto left_child = left_.GetNextChild();
      auto right_child = right_.GetNextChild();
      if (!left_child) {
        right_.SkipTo(min_key);
        return Status::OK;
      }
      if (!right_child) {
        left_.SkipTo(min_key);
        return Status::OK;
      }
      if (left_child == right_child) {
        return Status::OK;
      }
      // Same nodes might be in different depths of the btrees. Only descend in
      // each if their current level is the same as or greater than the other
      // one's.
      uint8_t level_left = left_.GetLevel();
      uint8_t level_right = right_.GetLevel();
      if (level_left >= level_right) {
        RETURN_ON_ERROR(left_.Advance());
      }
      if (level_right >= level_left) {
        RETURN_ON_ERROR(right_.Advance());
      }
    }
  }

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
    FXL_DCHECK(IsNormalized());
    return (right_.HasValue() && (left_.Finished() || left_.HasValue())) ||
           (left_.HasValue() && HasSameNextChild());
  }

  // Returns whether the 2 iterators have the same next child in the
  // iteration. This allows to skip part of the 2 btrees when they are
  // identicals.
  bool HasSameNextChild() const {
    if (left_.Finished()) {
      return false;
    }
    auto right_child = right_.GetNextChild();
    if (!right_child) {
      return false;
    }
    auto left_child = left_.GetNextChild();
    if (!left_child) {
      return false;
    }
    return *right_child == *left_child;
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
  bool SendRight() { return Send(&right_, &left_, !diff_from_left_to_right_); }

  // Send a diff using the left iterator.
  bool SendLeft() { return Send(&left_, &right_, diff_from_left_to_right_); }

  bool Send(BTreeIterator* it1, BTreeIterator* it2, bool it1_to_it2) {
    std::unique_ptr<Entry> it1_entry, it2_entry;
    it1_entry = std::make_unique<Entry>(it1->CurrentEntry());
    if (!it2->Finished() && it2->HasValue() &&
        it1->CurrentEntry().key == it2->CurrentEntry().key) {
      it2_entry = std::make_unique<Entry>(it2->CurrentEntry());
    }

    if (it1_to_it2) {
      return on_next_(std::move(it1_entry), std::move(it2_entry));
    }
    return on_next_(std::move(it2_entry), std::move(it1_entry));
  }

  fit::function<bool(std::unique_ptr<Entry>, std::unique_ptr<Entry>)> on_next_;
  BTreeIterator left_;
  BTreeIterator right_;
  // Keep track whether the change is from left to right, or right to left.
  // This allows to switch left and right during the algorithm to handle less
  // cases.
  bool diff_from_left_to_right_ = true;
};

// Iterator that does a three-way diff by using two IteratorPair objects in
// parallel.
// Here is an attempt to convey what this iterator should be doing:
// - It creates an IteratorPair (IP thereafter) for each side of the diff
//   (base-to-left and base-to-right).
// - At the initialization time, it advances each internal IP to their first
//   diff. Each IP (as viewed from here) is on one key: the key of the latest
//   diff it returned.
// - We always advance the IP with the lowest key, or the one not finished yet.
//   If both are on the same key, we advance both.
// - The current key considered by the ThreeWayIterator is the lowest key of the
//   latest left and right diffs. If one IP is finished, then the current key is
//   the key of the other IP's diff.
// - When sending the ThreeWayIterator diff, we consider the current key (per
//   above). If both IPs are on the same key, the diff is easy (we just send
//   everything). However, if the IPs are on different keys, or one of them is
//   finished, we have to consider multiple cases:
//   - If the base entry is present, it means the key/value was present in the
//     base revision. Given that the other IP moved past this key, there is no
//     diff on that side and we copy the base entry to that side entry within
//     the tree-way diff change.
//   - If the base entry is not present, it means the key/value was not present
//     in the base revision and it is an addition.
class ThreeWayIterator {
 public:
  explicit ThreeWayIterator(SynchronousStorage* storage) {
    base_left_iterators_ = std::make_unique<IteratorPair>(
        storage,
        [this](std::unique_ptr<Entry> base, std::unique_ptr<Entry> left) {
          left_advanced_ = true;
          base_left_.swap(base);
          left_.swap(left);
          return true;
        });
    base_right_iterators_ = std::make_unique<IteratorPair>(
        storage,
        [this](std::unique_ptr<Entry> base, std::unique_ptr<Entry> right) {
          right_advanced_ = true;
          base_right_.swap(base);
          right_.swap(right);
          return true;
        });
  }

  Status Init(ObjectIdentifier base_node_identifier,
              ObjectIdentifier left_node_identifier,
              ObjectIdentifier right_node_identifier, fxl::StringView min_key) {
    RETURN_ON_ERROR(base_left_iterators_->Init(base_node_identifier,
                                               left_node_identifier, min_key));
    RETURN_ON_ERROR(base_right_iterators_->Init(
        base_node_identifier, right_node_identifier, min_key));
    if (!Finished()) {
      RETURN_ON_ERROR(AdvanceLeft());
      RETURN_ON_ERROR(AdvanceRight());
    }
    return Status::OK;
  }

  bool Finished() const {
    return base_left_iterators_->Finished() &&
           base_right_iterators_->Finished() && !base_left_ && !left_ &&
           !base_right_ && !right_;
  }

  Status Advance() {
    FXL_DCHECK(!Finished());
    if (base_left_iterators_->Finished() && !base_left_ && !left_) {
      RETURN_ON_ERROR(AdvanceRight());
    } else if (base_right_iterators_->Finished() && !base_right_ && !right_) {
      RETURN_ON_ERROR(AdvanceLeft());
    } else if (GetLeftKey() < GetRightKey()) {
      RETURN_ON_ERROR(AdvanceLeft());
    } else if (GetLeftKey() > GetRightKey()) {
      RETURN_ON_ERROR(AdvanceRight());
    } else {
      RETURN_ON_ERROR(AdvanceLeft());
      RETURN_ON_ERROR(AdvanceRight());
    }
    return Status::OK;
  }

  ThreeWayChange GetCurrentDiff() {
    FXL_DCHECK(!Finished());
    ThreeWayChange change;
    change.base = GetBase();
    change.left = GetEntry(change.base, base_left_iterators_, left_, right_);
    change.right = GetEntry(change.base, base_right_iterators_, right_, left_);
    return change;
  }

 private:
  // GetLeftKey (resp. GetRightKey) should not be called if the left (resp.
  // right) IteratorPair is finished.
  const std::string& GetLeftKey() {
    FXL_DCHECK(base_left_ || left_);
    if (base_left_) {
      return base_left_->key;
    }
    return left_->key;
  }

  const std::string& GetRightKey() {
    FXL_DCHECK(base_right_ || right_);
    if (base_right_) {
      return base_right_->key;
    }
    return right_->key;
  }

  Status AdvanceLeft() {
    left_advanced_ = false;
    while (!base_left_iterators_->Finished() && !left_advanced_) {
      if (!base_left_iterators_->SendDiff()) {
        return Status::OK;
      }
      RETURN_ON_ERROR(base_left_iterators_->Advance());
    }
    if (!left_advanced_ && base_left_iterators_->Finished()) {
      base_left_.reset();
      left_.reset();
    }
    return Status::OK;
  }

  Status AdvanceRight() {
    right_advanced_ = false;
    while (!base_right_iterators_->Finished() && !right_advanced_) {
      if (!base_right_iterators_->SendDiff()) {
        return Status::OK;
      }
      RETURN_ON_ERROR(base_right_iterators_->Advance());
    }
    if (!right_advanced_ && base_right_iterators_->Finished()) {
      base_right_.reset();
      right_.reset();
    }
    return Status::OK;
  }

  std::unique_ptr<Entry> GetBase() {
    if (base_left_ && base_right_) {
      if (base_left_->key < base_right_->key) {
        return std::make_unique<Entry>(*base_left_);
      }
      return std::make_unique<Entry>(*base_right_);
    }
    if (!base_right_ && base_left_ &&
        (!right_ || base_left_->key < right_->key)) {
      return std::make_unique<Entry>(*base_left_);
    }
    if (!base_left_ && base_right_ &&
        (!left_ || base_right_->key < left_->key)) {
      return std::make_unique<Entry>(*base_right_);
    }
    return std::unique_ptr<Entry>();
  }

  std::unique_ptr<Entry> GetEntry(
      const std::unique_ptr<Entry>& base,
      const std::unique_ptr<IteratorPair>& this_iterator,
      const std::unique_ptr<Entry>& this_entry,
      const std::unique_ptr<Entry>& other_entry) {
    if (base) {
      if (this_entry && base->key == this_entry->key) {
        return std::make_unique<Entry>(*this_entry);
      }
      if (this_entry && base->key < this_entry->key) {
        return std::make_unique<Entry>(*base);
      }
      if (this_iterator->Finished()) {
        return std::make_unique<Entry>(*base);
      }
      return std::unique_ptr<Entry>();
    }
    if (!this_entry || (other_entry && other_entry->key < this_entry->key)) {
      return std::unique_ptr<Entry>();
    }
    return std::make_unique<Entry>(*this_entry);
  }

  bool left_advanced_ = false;
  bool right_advanced_ = false;

  std::unique_ptr<Entry> base_left_;
  std::unique_ptr<Entry> base_right_;
  std::unique_ptr<Entry> left_;
  std::unique_ptr<Entry> right_;

  std::unique_ptr<IteratorPair> base_left_iterators_;
  std::unique_ptr<IteratorPair> base_right_iterators_;
};

Status ForEachDiffInternal(SynchronousStorage* storage,
                           ObjectIdentifier left_node_identifier,
                           ObjectIdentifier right_node_identifier,
                           std::string min_key,
                           fit::function<bool(EntryChange)> on_next) {
  FXL_DCHECK(storage::IsDigestValid(left_node_identifier.object_digest));
  FXL_DCHECK(storage::IsDigestValid(right_node_identifier.object_digest));

  if (left_node_identifier == right_node_identifier) {
    return Status::OK;
  }

  auto wrapped_next = [on_next = std::move(on_next)](
                          std::unique_ptr<Entry> base,
                          std::unique_ptr<Entry> other) {
    if (other) {
      return on_next({std::move(*other), false});
    }
    return on_next({std::move(*base), true});
  };

  IteratorPair iterators(storage, std::move(wrapped_next));
  RETURN_ON_ERROR(
      iterators.Init(left_node_identifier, right_node_identifier, min_key));

  while (!iterators.Finished()) {
    if (!iterators.SendDiff()) {
      return Status::OK;
    }
    RETURN_ON_ERROR(iterators.Advance());
  }

  return Status::OK;
}  // namespace

Status ForEachThreeWayDiffInternal(
    SynchronousStorage* storage, ObjectIdentifier base_node_identifier,
    ObjectIdentifier left_node_identifier,
    ObjectIdentifier right_node_identifier, std::string min_key,
    fit::function<bool(ThreeWayChange)> on_next) {
  FXL_DCHECK(IsDigestValid(base_node_identifier.object_digest));
  FXL_DCHECK(IsDigestValid(left_node_identifier.object_digest));
  FXL_DCHECK(IsDigestValid(right_node_identifier.object_digest));

  if (left_node_identifier == right_node_identifier) {
    return Status::OK;
  }

  ThreeWayIterator iterator(storage);
  RETURN_ON_ERROR(iterator.Init(base_node_identifier, left_node_identifier,
                                right_node_identifier, min_key));

  while (!iterator.Finished()) {
    if (!on_next(iterator.GetCurrentDiff())) {
      return Status::OK;
    }
    RETURN_ON_ERROR(iterator.Advance());
  }

  return Status::OK;
}

}  // namespace

void ForEachDiff(coroutine::CoroutineService* coroutine_service,
                 PageStorage* page_storage,
                 ObjectIdentifier base_root_identifier,
                 ObjectIdentifier other_root_identifier, std::string min_key,
                 fit::function<bool(EntryChange)> on_next,
                 fit::function<void(Status)> on_done) {
  FXL_DCHECK(storage::IsDigestValid(base_root_identifier.object_digest));
  FXL_DCHECK(storage::IsDigestValid(other_root_identifier.object_digest));
  coroutine_service->StartCoroutine(
      [page_storage, base_root_identifier = std::move(base_root_identifier),
       other_root_identifier = std::move(other_root_identifier),
       on_next = std::move(on_next), min_key = std::move(min_key),
       on_done =
           std::move(on_done)](coroutine::CoroutineHandler* handler) mutable {
        SynchronousStorage storage(page_storage, handler);

        on_done(ForEachDiffInternal(&storage, base_root_identifier,
                                    other_root_identifier, std::move(min_key),
                                    std::move(on_next)));
      });
}

void ForEachThreeWayDiff(coroutine::CoroutineService* coroutine_service,
                         PageStorage* page_storage,
                         ObjectIdentifier base_root_identifier,
                         ObjectIdentifier left_root_identifier,
                         ObjectIdentifier right_root_identifier,
                         std::string min_key,
                         fit::function<bool(ThreeWayChange)> on_next,
                         fit::function<void(Status)> on_done) {
  FXL_DCHECK(storage::IsDigestValid(base_root_identifier.object_digest));
  FXL_DCHECK(storage::IsDigestValid(left_root_identifier.object_digest));
  FXL_DCHECK(storage::IsDigestValid(right_root_identifier.object_digest));
  coroutine_service->StartCoroutine(
      [page_storage, base_root_identifier = std::move(base_root_identifier),
       left_root_identifier = std::move(left_root_identifier),
       right_root_identifier = std::move(right_root_identifier),
       on_next = std::move(on_next), min_key = std::move(min_key),
       on_done =
           std::move(on_done)](coroutine::CoroutineHandler* handler) mutable {
        SynchronousStorage storage(page_storage, handler);

        on_done(ForEachThreeWayDiffInternal(
            &storage, base_root_identifier, left_root_identifier,
            right_root_identifier, std::move(min_key), std::move(on_next)));
      });
}

}  // namespace btree
}  // namespace storage
