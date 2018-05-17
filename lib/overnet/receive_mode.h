// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitset>
#include <map>
#include <unordered_map>
#include "ack_frame.h"
#include "callback.h"
#include "optional.h"
#include "reliability_and_ordering.h"
#include "status.h"

namespace overnet {
namespace receive_mode {

using BeginCallback = Callback<Status, 16 * sizeof(void*)>;

class ReceiveMode {
 public:
  virtual ~ReceiveMode() {}
  // A new message is available. Ready will be called when a decision on message
  // acceptance has been made.
  virtual void Begin(uint64_t seq, BeginCallback ready) = 0;
  // Once a sequence has begun (Begin() called, ready callback made
  // *successfully*), Completed must be called exactly once.
  virtual void Completed(uint64_t seq, const Status& status) = 0;
  // Close the receive mode: all pending things finish
  virtual void Close(const Status& status) = 0;
};

class ReliableOrdered final : public ReceiveMode {
 public:
  ReliableOrdered() = default;
  ReliableOrdered(const ReliableOrdered&) = delete;
  ReliableOrdered& operator=(const ReliableOrdered&) = delete;

  void Begin(uint64_t seq, BeginCallback ready) override;
  void Completed(uint64_t seq, const Status& status) override;
  void Close(const Status& status) override;

 private:
  uint64_t cur_ = 1;
  bool cur_in_progress_ = false;
  std::unordered_map<uint64_t, BeginCallback> later_;
  Optional<Status> closed_;
};

class ReliableUnordered final : public ReceiveMode {
 public:
  ReliableUnordered() = default;
  ReliableUnordered(const ReliableUnordered&) = delete;
  ReliableUnordered& operator=(const ReliableUnordered&) = delete;

  static constexpr size_t kLookaheadWindow = 128;

  void Begin(uint64_t seq, BeginCallback ready) override;
  void Completed(uint64_t seq, const Status& status) override;
  void Close(const Status& status) override;

 private:
  uint64_t tip_ = 1;
  std::bitset<kLookaheadWindow> in_progress_;
  std::bitset<kLookaheadWindow> done_;
  Optional<Status> closed_;
};

class UnreliableOrdered final : public ReceiveMode {
 public:
  UnreliableOrdered() = default;
  UnreliableOrdered(const UnreliableOrdered&) = delete;
  UnreliableOrdered& operator=(const UnreliableOrdered&) = delete;

  void Begin(uint64_t seq, BeginCallback ready) override;
  void Completed(uint64_t seq, const Status& status) override;
  void Close(const Status& status) override;

 private:
  uint64_t cur_ = 1;
  bool cur_in_progress_ = false;
  std::map<uint64_t, BeginCallback> later_;
  Optional<Status> closed_;
};

class UnreliableUnordered final : public ReceiveMode {
 public:
  UnreliableUnordered() = default;
  UnreliableUnordered(const UnreliableUnordered&) = delete;
  UnreliableUnordered& operator=(const UnreliableUnordered&) = delete;

  static constexpr size_t kLookaheadWindow = 256;

  void Begin(uint64_t seq, BeginCallback ready) override;
  void Completed(uint64_t seq, const Status& status) override;
  void Close(const Status& status) override;

 private:
  uint64_t tip_ = 1;
  std::bitset<kLookaheadWindow> in_progress_;
  Optional<Status> closed_;
};

class TailReliable final : public ReceiveMode {
 public:
  TailReliable() = default;
  TailReliable(const TailReliable&) = delete;
  TailReliable& operator=(const TailReliable&) = delete;

  void Begin(uint64_t seq, BeginCallback ready) override;
  void Completed(uint64_t seq, const Status& status) override;
  void Close(const Status& status) override;

 private:
  uint64_t cur_ = 1;
  bool cur_in_progress_ = false;
  std::map<uint64_t, BeginCallback> later_;
  Optional<Status> closed_;
};

class Error final : public ReceiveMode {
 public:
  Error() = default;
  Error(const Error&) = delete;
  Error& operator=(const Error&) = delete;

  void Begin(uint64_t seq, BeginCallback ready) override;
  void Completed(uint64_t seq, const Status& status) override;
  void Close(const Status& status) override;
};

class ParameterizedReceiveMode final : public ReceiveMode {
 public:
  explicit ParameterizedReceiveMode(
      ReliabilityAndOrdering reliability_and_ordering)
      : storage_(), receive_mode_(storage_.Init(reliability_and_ordering)) {}
  ~ParameterizedReceiveMode() { receive_mode_->~ReceiveMode(); }
  void Begin(uint64_t seq, BeginCallback ready) override {
    return receive_mode_->Begin(seq, std::move(ready));
  }
  void Completed(uint64_t seq, const Status& status) override {
    return receive_mode_->Completed(seq, status);
  }
  void Close(const Status& status) override { receive_mode_->Close(status); }

  ReceiveMode* get() const { return receive_mode_; }

 private:
  union Storage {
    Storage() {}
    ReceiveMode* Init(ReliabilityAndOrdering reliability_and_ordering);
    ~Storage() {}
    ReliableOrdered reliable_ordered;
    ReliableUnordered reliable_unordered;
    UnreliableOrdered unreliable_ordered;
    UnreliableUnordered unreliable_unordered;
    TailReliable tail_reliable;
    Error error;
  };
  Storage storage_;
  ReceiveMode* receive_mode_;
};

}  // namespace receive_mode
}  // namespace overnet
