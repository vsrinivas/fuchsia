// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "receive_mode.h"

namespace overnet {
namespace receive_mode {

static const uint64_t kMaxSeq = ~uint64_t(0);

///////////////////////////////////////////////////////////////////////////////
// ReliableOrdered

bool ReliableOrdered::Begin(uint64_t seq, StatusCallback ready) {
  if (seq > max_seen_) max_seen_ = seq;
  if (seq < cur_) return false;
  if (cur_ == seq) {
    if (!cur_in_progress_) {
      cur_in_progress_ = true;
      ready(Status::Ok());
    }
    return false;
  } else {
    later_[seq] = std::move(ready);
    return true;
  }
}

bool ReliableOrdered::Completed(uint64_t seq, const Status& status) {
  assert(seq == cur_ && cur_in_progress_);
  if (status.is_ok()) {
    if (cur_ == kMaxSeq) {
      return true;
    } else {
      cur_++;
      cur_in_progress_ = false;
      auto it = later_.find(cur_);
      if (it != later_.end()) {
        cur_in_progress_ = true;
        auto cb = std::move(it->second);
        later_.erase(it);
        cb(Status::Ok());
      }
      return true;
    }
  } else {
    cur_in_progress_ = false;
    return false;
  }
}

AckFrame ReliableOrdered::GenerateAck() const {
  AckFrame a(cur_);
  if (!cur_in_progress_ && max_seen_ > cur_) a.AddNack(cur_);
  for (uint64_t i = cur_ + 1; i < std::min(cur_ + 128, max_seen_); i++) {
    if (later_.count(i) == 0) a.AddNack(i);
  }
  return a;
}

///////////////////////////////////////////////////////////////////////////////
// ReliableUnordered

bool ReliableUnordered::Begin(uint64_t seq, StatusCallback ready) {
  if (seq < tip_) return false;
  if (seq >= tip_ + kLookaheadWindow) return false;
  // After window check => always legal index in GenerateAck
  if (seq > max_seen_) max_seen_ = seq;
  auto idx = seq - tip_;
  if (in_progress_.test(idx)) return false;
  in_progress_.set(idx);
  ready(Status::Ok());
  for (uint64_t i = 0; i < idx; i++) {
    if (!in_progress_.test(i)) return true;
  }
  return false;
}

bool ReliableUnordered::Completed(uint64_t seq, const Status& status) {
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
      return true;
    } else {
      return false;
    }
  } else {
    in_progress_.reset(idx);
    return true;
  }
}

AckFrame ReliableUnordered::GenerateAck() const {
  AckFrame a(tip_);
  for (uint64_t i = 0; i < std::max(tip_, max_seen_) - tip_; i++) {
    if (!in_progress_[i]) a.AddNack(i + tip_);
  }
  return a;
}

///////////////////////////////////////////////////////////////////////////////
// UnreliableOrdered

bool UnreliableOrdered::Begin(uint64_t seq, StatusCallback ready) {
  if (seq > max_seen_) max_seen_ = seq;
  if (seq < cur_) return false;
  if (seq > cur_ && cur_in_progress_) {
    later_[seq] = std::move(ready);
    return false;
  }
  assert(seq >= cur_);
  if (cur_in_progress_) return false;
  cur_in_progress_ = true;
  cur_ = seq;
  ready(Status::Ok());
  return !later_.empty() && later_.begin()->first > cur_ + 1;
}

bool UnreliableOrdered::Completed(uint64_t seq, const Status& status) {
  assert(seq == cur_);
  assert(cur_in_progress_);
  cur_in_progress_ = false;
  if (!later_.empty()) {
    auto it = later_.begin();
    uint64_t later_seq = it->first;
    StatusCallback later_cb = std::move(it->second);
    later_.erase(it);
    assert(later_seq > cur_);
    cur_ = later_seq;
    cur_in_progress_ = true;
    later_cb(Status::Ok());
    return !later_.empty() && later_.begin()->first > cur_ + 1;
  } else {
    if (status.is_ok() && cur_ != kMaxSeq) cur_++;
    return true;
  }
}

AckFrame UnreliableOrdered::GenerateAck() const {
  AckFrame f(cur_);
  if (!cur_in_progress_ && max_seen_ >= cur_) f.AddNack(cur_);
  int n = 0;
  if (!later_.empty()) {
    uint64_t up_to = cur_;
    for (const auto& el : later_) {
      while (up_to < el.first) {
        f.AddNack(up_to);
        up_to++;
        if (n++ > 30) return f;
      }
      up_to++;
    }
  }
  return f;
}

///////////////////////////////////////////////////////////////////////////////
// UnreliableUnordered

bool UnreliableUnordered::Begin(uint64_t seq, StatusCallback ready) {
  if (seq < tip_) return false;
  bool moved_tip = false;
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
    moved_tip = true;
  }
  // After window check => always legal index in GenerateAck.
  if (seq > max_seen_) max_seen_ = seq;
  if (!in_progress_.test(seq - tip_)) {
    in_progress_.set(seq - tip_);
    ready(Status::Ok());
  }
  return moved_tip;
}

bool UnreliableUnordered::Completed(uint64_t seq, const Status& status) {
  if (seq < tip_) return false;
  bool moved_tip = false;
  if (status.is_ok()) {
    if (seq == tip_) {
      if (tip_ != kMaxSeq) {
        tip_++;
        in_progress_ >>= 1;
      }
      moved_tip = true;
    }
  } else {
    in_progress_.reset(seq - tip_);
  }
  return moved_tip;
}

AckFrame UnreliableUnordered::GenerateAck() const {
  AckFrame a(tip_);
  for (uint64_t i = 0; i < std::max(tip_, max_seen_) - tip_; i++) {
    if (!in_progress_[i]) a.AddNack(i + tip_);
  }
  return a;
}

///////////////////////////////////////////////////////////////////////////////
// TailReliable

bool TailReliable::Begin(uint64_t seq, StatusCallback ready) {
  if (seq > max_seen_) max_seen_ = seq;
  if (seq < cur_) return false;
  if (seq > cur_ && cur_in_progress_) {
    later_[seq] = std::move(ready);
    return false;
  }
  assert(seq >= cur_);
  if (cur_in_progress_) return false;
  cur_in_progress_ = true;
  cur_ = seq;
  ready(Status::Ok());
  return !later_.empty() && later_.begin()->first > cur_ + 1;
}

bool TailReliable::Completed(uint64_t seq, const Status& status) {
  assert(seq == cur_);
  assert(cur_in_progress_);
  cur_in_progress_ = false;
  if (!later_.empty()) {
    auto it = later_.begin();
    uint64_t later_seq = it->first;
    StatusCallback later_cb = std::move(it->second);
    later_.erase(it);
    assert(later_seq > cur_);
    cur_ = later_seq;
    cur_in_progress_ = true;
    later_cb(Status::Ok());
    return !later_.empty() && later_.begin()->first > cur_ + 1;
  } else {
    if (status.is_ok() && cur_ != kMaxSeq) cur_++;
    return true;
  }
}

AckFrame TailReliable::GenerateAck() const {
  AckFrame f(cur_);
  if (!cur_in_progress_ && max_seen_ >= cur_) f.AddNack(cur_);
  int n = 0;
  if (!later_.empty()) {
    uint64_t up_to = cur_;
    for (const auto& el : later_) {
      while (up_to < el.first) {
        f.AddNack(up_to);
        up_to++;
        if (n++ > 30) return f;
      }
      up_to++;
    }
  }
  return f;
}

///////////////////////////////////////////////////////////////////////////////
// Error

bool Error::Begin(uint64_t seq, StatusCallback ready) {
  ready(Status::Cancelled());
  return false;
}
bool Error::Completed(uint64_t seq, const Status& status) {
  abort();
  return false;
}
AckFrame Error::GenerateAck() const { return AckFrame(1); }

///////////////////////////////////////////////////////////////////////////////
// ParameterizedReceiveMode

ReceiveMode* ParameterizedReceiveMode::Storage::Init(
    ReliabilityAndOrdering reliability_and_ordering) {
  switch (reliability_and_ordering) {
    case ReliabilityAndOrdering::ReliableOrdered:
      return new (&reliable_ordered) ReliableOrdered();
      break;
    case ReliabilityAndOrdering::ReliableUnordered:
      return new (&reliable_unordered) ReliableUnordered();
      break;
    case ReliabilityAndOrdering::UnreliableOrdered:
      return new (&unreliable_ordered) UnreliableOrdered();
      break;
    case ReliabilityAndOrdering::UnreliableUnordered:
      return new (&unreliable_unordered) UnreliableUnordered();
      break;
    case ReliabilityAndOrdering::TailReliable:
      return new (&tail_reliable) TailReliable();
      break;
    default:
      return new (&error) Error();
      break;
  }
}

}  // namespace receive_mode
}  // namespace overnet
