// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <set>

#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/links/packet_nub.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"

namespace overnet {

class PacketNubFuzzer {
 public:
  PacketNubFuzzer(bool logging);
  // Process one slice of data from address 'src'
  void Process(uint64_t src, Slice slice);
  // Step time forward
  bool StepTime(uint64_t microseconds);

 private:
  TestTimer timer_;

  // Until a connection is formed, we track a packet budget per address: a nub
  // is not allowed to send more than its budget to any address.
  // This allows us to check for packet reflection/amplification attacks via the
  // fuzzer infrastructure.
  // Budgets are tracked as an allowance to send one packet up to 'bytes' in
  // size.
  class Budget {
   public:
    void AddBudget(uint64_t address, uint64_t bytes);
    void ConsumeBudget(uint64_t address, uint64_t bytes);

   private:
    // Map of address to a queue of packet sizes the sender is allowed.
    std::map<uint64_t, std::multiset<uint64_t>> budget_;
  };
  Budget budget_;

  using BaseNub = PacketNub<uint64_t, 256>;
  class Nub : private Router, public BaseNub {
   public:
    Nub(Timer* timer);

    void Process(TimeStamp received, uint64_t src, Slice slice) override;

    void SendTo(uint64_t dest, Slice slice) override;
    void Publish(LinkPtr<> link) override;

   private:
    Budget budget_;
  };
  Nub nub_{&timer_};

  struct Logging {
    Logging(Timer* timer);
    TraceCout tracer;
    ScopedRenderer set_tracer{&tracer};
  };
  std::unique_ptr<Logging> logging_;
};

}  // namespace overnet
