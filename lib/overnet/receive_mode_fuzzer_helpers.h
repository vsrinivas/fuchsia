// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_set>
#include "receive_mode.h"

namespace overnet {
namespace receive_mode {

class Fuzzer {
 public:
  explicit Fuzzer(uint8_t type)
      : receive_mode_(static_cast<ReliabilityAndOrdering>(type)) {}

  void Step() { iteration_++; }

  bool Begin(uint64_t seq) {
    if (seq > max_seen_seq_) {
      max_seen_seq_ = seq;
      when_max_seen_ = iteration_;
    }
    if (begun_seqs_.count(seq) == 1) {
      bool saw_immediate_error = false;
      receive_mode_.Begin(
          seq, StatusCallback(ALLOCATED_CALLBACK,
                              [&saw_immediate_error](const Status& status) {
                                assert(status.is_error());
                                saw_immediate_error = true;
                              }));
      assert(saw_immediate_error);
    } else {
      receive_mode_.Begin(
          seq,
          StatusCallback(ALLOCATED_CALLBACK, [seq, this](const Status& status) {
            if (status.is_ok()) {
              assert(begun_seqs_.count(seq) == 0);
              begun_seqs_.insert(seq);
            }
          }));
    }
    return true;
  }

  bool Completed(uint64_t seq, uint8_t status) {
    if (begun_seqs_.count(seq) == 0) return false;  // invalid byte sequence
    begun_seqs_.erase(seq);
    receive_mode_.Completed(seq, Status(static_cast<StatusCode>(status)));
    return true;
  }

 private:
  uint64_t iteration_ = 0;
  uint64_t max_seen_seq_ = 0;
  uint64_t when_max_seen_ = 0;
  ParameterizedReceiveMode receive_mode_;
  std::unordered_set<uint64_t> begun_seqs_;
};

}  // namespace receive_mode
}  // namespace overnet
