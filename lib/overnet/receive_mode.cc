// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "receive_mode.h"

namespace overnet {
namespace receive_mode {

static const uint64_t kMaxSeq = ~uint64_t(0);

///////////////////////////////////////////////////////////////////////////////
// ReliableOrdered

void ReliableOrdered::Begin(uint64_t seq, BeginCallback ready) {
  if (closed_.has_value()) {
    ready(closed_->OrCancelled());
    return;
  }
  if (seq < cur_) return;
  if (cur_ == seq) {
    if (!cur_in_progress_) {
      cur_in_progress_ = true;
      ready(Status::Ok());
    }
  } else {
    later_[seq] = std::move(ready);
  }
}

void ReliableOrdered::Completed(uint64_t seq, const Status& status) {
  assert(seq == cur_ && cur_in_progress_);
  if (status.is_ok()) {
    if (cur_ != kMaxSeq) {
      cur_++;
      cur_in_progress_ = false;
      auto it = later_.find(cur_);
      if (it != later_.end()) {
        cur_in_progress_ = true;
        auto cb = std::move(it->second);
        later_.erase(it);
        cb(Status::Ok());
      }
    }
  } else {
    cur_in_progress_ = false;
  }
}

void ReliableOrdered::Close(const Status& status) {
  if (closed_.has_value()) return;
  closed_ = status;
  std::unordered_map<uint64_t, BeginCallback> later;
  later_.swap(later);
  for (auto& p : later) {
    p.second(closed_->OrCancelled());
  }
}

///////////////////////////////////////////////////////////////////////////////
// ReliableUnordered

void ReliableUnordered::Begin(uint64_t seq, BeginCallback ready) {
  if (closed_.has_value()) {
    ready(closed_->OrCancelled());
    return;
  }
  if (seq < tip_) return;
  if (seq >= tip_ + kLookaheadWindow) return;
  auto idx = seq - tip_;
  if (in_progress_.test(idx)) return;
  in_progress_.set(idx);
  ready(Status::Ok());
  for (uint64_t i = 0; i < idx; i++) {
    if (!in_progress_.test(i)) return;
  }
}

void ReliableUnordered::Completed(uint64_t seq, const Status& status) {
  auto idx = seq - tip_;
  if (status.is_ok()) {
    done_.set(idx);
    if (idx == 0) {
      // TODO(ctiller): count how far to shift, and then do this, as the
      // shifts could be expensive.
      while (tip_ != kMaxSeq && done_.test(0)) {
        tip_++;
        in_progress_ >>= 1;
        done_ >>= 1;
      }
    }
  } else {
    in_progress_.reset(idx);
  }
}

void ReliableUnordered::Close(const Status& status) {
  if (closed_.has_value()) return;
  closed_ = status;
}

///////////////////////////////////////////////////////////////////////////////
// UnreliableOrdered

void UnreliableOrdered::Begin(uint64_t seq, BeginCallback ready) {
  if (closed_.has_value()) {
    ready(closed_->OrCancelled());
    return;
  }
  if (seq < cur_) return;
  if (seq > cur_ && cur_in_progress_) {
    later_[seq] = std::move(ready);
    return;
  }
  assert(seq >= cur_);
  if (cur_in_progress_) return;
  cur_in_progress_ = true;
  cur_ = seq;
  ready(Status::Ok());
}

void UnreliableOrdered::Completed(uint64_t seq, const Status& status) {
  assert(seq == cur_);
  assert(cur_in_progress_);
  cur_in_progress_ = false;
  if (!later_.empty()) {
    auto it = later_.begin();
    uint64_t later_seq = it->first;
    BeginCallback later_cb = std::move(it->second);
    later_.erase(it);
    assert(later_seq > cur_);
    cur_ = later_seq;
    cur_in_progress_ = true;
    later_cb(Status::Ok());
  } else {
    if (status.is_ok() && cur_ != kMaxSeq) cur_++;
  }
}

void UnreliableOrdered::Close(const Status& status) {
  if (closed_.has_value()) return;
  closed_ = status;
  std::map<uint64_t, BeginCallback> later;
  later_.swap(later);
  for (auto& p : later) {
    p.second(closed_->OrCancelled());
  }
}

///////////////////////////////////////////////////////////////////////////////
// UnreliableUnordered

void UnreliableUnordered::Begin(uint64_t seq, BeginCallback ready) {
  if (closed_.has_value()) {
    ready(closed_->OrCancelled());
    return;
  }
  if (seq < tip_) return;
  if (seq >= kLookaheadWindow && seq - kLookaheadWindow >= tip_) {
    uint64_t new_tip = seq - kLookaheadWindow + 1;
    assert(tip_ < new_tip);
    uint64_t move = new_tip - tip_;
    if (move > kLookaheadWindow) {
      in_progress_.reset();
    } else {
      in_progress_ >>= move;
    }
    tip_ = new_tip;
  }
  if (!in_progress_.test(seq - tip_)) {
    in_progress_.set(seq - tip_);
    ready(Status::Ok());
  }
}

void UnreliableUnordered::Completed(uint64_t seq, const Status& status) {
  if (seq < tip_) return;
  if (status.is_ok()) {
    if (seq == tip_) {
      if (tip_ != kMaxSeq) {
        tip_++;
        in_progress_ >>= 1;
      }
    }
  } else {
    in_progress_.reset(seq - tip_);
  }
}

void UnreliableUnordered::Close(const Status& status) {
  if (closed_.has_value()) return;
  closed_ = status;
}

///////////////////////////////////////////////////////////////////////////////
// TailReliable

void TailReliable::Begin(uint64_t seq, BeginCallback ready) {
  if (closed_.has_value()) {
    ready(closed_->OrCancelled());
    return;
  }
  if (seq < cur_) return;
  if (seq > cur_ && cur_in_progress_) {
    later_[seq] = std::move(ready);
    return;
  }
  assert(seq >= cur_);
  if (cur_in_progress_) return;
  cur_in_progress_ = true;
  cur_ = seq;
  ready(Status::Ok());
}

void TailReliable::Completed(uint64_t seq, const Status& status) {
  assert(seq == cur_);
  assert(cur_in_progress_);
  cur_in_progress_ = false;
  if (!later_.empty()) {
    auto it = later_.begin();
    uint64_t later_seq = it->first;
    BeginCallback later_cb = std::move(it->second);
    later_.erase(it);
    assert(later_seq > cur_);
    cur_ = later_seq;
    cur_in_progress_ = true;
    later_cb(Status::Ok());
  } else {
    if (status.is_ok() && cur_ != kMaxSeq) cur_++;
  }
}

void TailReliable::Close(const Status& status) {
  if (closed_.has_value()) return;
  closed_ = status;
  std::map<uint64_t, BeginCallback> later;
  later_.swap(later);
  for (auto& p : later) {
    p.second(closed_->OrCancelled());
  }
}

///////////////////////////////////////////////////////////////////////////////
// Error

void Error::Begin(uint64_t seq, BeginCallback ready) {
  ready(Status::Cancelled());
}

void Error::Completed(uint64_t seq, const Status& status) { abort(); }

void Error::Close(const Status& status) {}

///////////////////////////////////////////////////////////////////////////////
// ParameterizedReceiveMode

ReceiveMode* ParameterizedReceiveMode::Storage::Init(
    ReliabilityAndOrdering reliability_and_ordering) {
  switch (reliability_and_ordering) {
    case ReliabilityAndOrdering::ReliableOrdered:
      return new (&reliable_ordered) ReliableOrdered();
    case ReliabilityAndOrdering::ReliableUnordered:
      return new (&reliable_unordered) ReliableUnordered();
    case ReliabilityAndOrdering::UnreliableOrdered:
      return new (&unreliable_ordered) UnreliableOrdered();
    case ReliabilityAndOrdering::UnreliableUnordered:
      return new (&unreliable_unordered) UnreliableUnordered();
    case ReliabilityAndOrdering::TailReliable:
      return new (&tail_reliable) TailReliable();
    default:
      return new (&error) Error();
  }
}

}  // namespace receive_mode
}  // namespace overnet
