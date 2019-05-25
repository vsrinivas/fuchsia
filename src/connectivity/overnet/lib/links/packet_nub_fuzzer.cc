// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/packet_nub_fuzzer.h"

#include <stdio.h>

namespace overnet {

PacketNubFuzzer::PacketNubFuzzer(bool logging)
    : logging_(logging ? new Logging(&timer_) : nullptr) {}

void PacketNubFuzzer::Process(uint64_t src, Slice slice) {
  nub_.Process(timer_.Now(), src, std::move(slice));
}

bool PacketNubFuzzer::StepTime(uint64_t microseconds) {
  timer_.Step(microseconds);
  return timer_.Now().after_epoch() != TimeDelta::PositiveInf();
}

void PacketNubFuzzer::Budget::AddBudget(uint64_t address, uint64_t bytes) {
  budget_[address].emplace(bytes);
}

void PacketNubFuzzer::Budget::ConsumeBudget(uint64_t address, uint64_t bytes) {
  auto& allowed = budget_[address];
  assert(!allowed.empty());
  auto it = allowed.lower_bound(bytes);
  // The bytes allocated in the budget are for the next packet sent: if those
  // bytes are not used, they do not stack.
  assert(it != allowed.end());
  allowed.erase(it);
}

PacketNubFuzzer::Nub::Nub(Timer* timer)
    : Router(timer, NodeId(1), false), BaseNub(this) {}

void PacketNubFuzzer::Nub::Process(TimeStamp received, uint64_t src,
                                   Slice slice) {
  if (!HasConnectionTo(src)) {
    budget_.AddBudget(src, slice.length());
  }
  BaseNub::Process(received, src, std::move(slice));
}

void PacketNubFuzzer::Nub::SendTo(uint64_t dest, Slice slice) {
  if (!HasConnectionTo(dest)) {
    budget_.ConsumeBudget(dest, slice.length());
  }
}

void PacketNubFuzzer::Nub::Publish(LinkPtr<> link) {
  auto node = link->GetLinkStatus().from;
  if (NodeId(node) != NodeId(1)) {
    abort();
  }
  // We immediately drop the link because other fuzzers should probe those bits.
  // Here we just want to concentrate on the connection protocol.
}

PacketNubFuzzer::Logging::Logging(Timer* timer) : tracer(timer) {}

}  // namespace overnet
