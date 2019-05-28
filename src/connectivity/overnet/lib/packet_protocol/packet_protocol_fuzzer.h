// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iostream>
#include <map>
#include <random>

#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/packet_protocol/packet_protocol.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"
#include "src/connectivity/overnet/lib/vocabulary/closed_ptr.h"

namespace overnet {

class PacketProtocolFuzzer {
 public:
  PacketProtocolFuzzer(const PacketProtocol::Codec* codec, bool log_stuff)
      : logging_(log_stuff ? new Logging(&timer_) : nullptr), codec_(codec) {}
  ~PacketProtocolFuzzer() { done_ = true; }

  // Begin a send operation on the packet protocol.
  // Return true if the fuzzer should continue.
  bool BeginSend(uint8_t sender_idx, Slice data);

  // Finish one send operation.
  // Return true if the fuzzer should continue.
  bool CompleteSend(uint8_t sender_idx, uint8_t status);

  // Step time forward.
  // Return true if the fuzzer should continue.
  bool StepTime(int64_t microseconds) {
    return timer_.Step(
               std::min(microseconds, TimeDelta::FromSeconds(10).as_us())) &&
           timer_.Now().after_epoch() != TimeDelta::PositiveInf();
  }

 private:
  static constexpr inline uint32_t kMaxSegmentSize = 1500;

  class Sender final : public PacketProtocol::PacketSender {
   public:
    void SendPacket(SeqNum seq, LazySlice data) override;
    void NoConnectivity() override{};

    struct PendingSend {
      SeqNum seq;
      LazySlice data;
    };

    Optional<PendingSend> CompleteSend();

   private:
    std::map<uint64_t, PendingSend> pending_sends_;
    uint64_t next_send_id_ = 0;
  };

  // Retrieve Sender index 'idx'
  Optional<Sender*> sender(uint8_t idx);

  // Retrieve PacketProtocol index 'idx'
  Optional<PacketProtocol*> packet_protocol(uint8_t idx);

  bool done_ = false;
  TestTimer timer_;
  struct Logging {
    Logging(Timer* timer) : tracer(timer) {}
    TraceCout tracer;
    ScopedRenderer set_tracer{&tracer};
    ScopedSeverity set_severity{Severity::DEBUG};
  };
  std::unique_ptr<Logging> logging_;
  Sender sender1_;
  Sender sender2_;
  // PacketProtocol needs a random number generator for some timing data.
  // Create one here with a well known seed to keep things deterministic.
  std::mt19937 rng_{12345};
  const PacketProtocol::Codec* const codec_;
  ClosedPtr<PacketProtocol> pp1_ = MakeClosedPtr<PacketProtocol>(
      &timer_, [this] { return rng_(); }, &sender1_, codec_, kMaxSegmentSize,
      true);
  ClosedPtr<PacketProtocol> pp2_ = MakeClosedPtr<PacketProtocol>(
      &timer_, [this] { return rng_(); }, &sender2_, codec_, kMaxSegmentSize,
      true);
};

}  // namespace overnet
