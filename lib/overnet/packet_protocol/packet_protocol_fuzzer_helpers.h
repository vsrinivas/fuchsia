// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iostream>
#include <map>
#include <random>
#include "garnet/lib/overnet/packet_protocol/packet_protocol.h"
#include "garnet/lib/overnet/testing/test_timer.h"
#include "garnet/lib/overnet/testing/trace_cout.h"
#include "garnet/lib/overnet/vocabulary/closed_ptr.h"

namespace overnet {

class PacketProtocolFuzzer {
 public:
  PacketProtocolFuzzer(const PacketProtocol::Codec* codec, bool log_stuff)
      : logging_(log_stuff ? new Logging(&timer_) : nullptr), codec_(codec) {}
  ~PacketProtocolFuzzer() { done_ = true; }

  bool BeginSend(uint8_t sender_idx, Slice data) {
    return packet_protocol(sender_idx).Then([this, data](PacketProtocol* pp) {
      pp->Send(
          [data](auto arg) { return data; },
          [this](const Status& status) {
            if (done_)
              return;
            if (!status.is_ok() && status.code() != StatusCode::CANCELLED) {
              std::cerr << "Expected each send to be ok or cancelled, got: "
                        << status << "\n";
              abort();
            }
          });
      return true;
    });
  }

  bool CompleteSend(uint8_t sender_idx, uint8_t status) {
    Optional<Sender::PendingSend> send =
        sender(sender_idx).Then([status](Sender* sender) {
          return sender->CompleteSend(status);
        });
    if (!send)
      return false;
    if (status == 0) {
      auto slice = send->data(LazySliceArgs{
          Border::None(), std::numeric_limits<uint32_t>::max(), false});
      auto process_status = (*packet_protocol(3 - sender_idx))
                                ->Process(timer_.Now(), send->seq, slice);
      if (process_status.status.is_error()) {
        std::cerr << "Expected Process() to return ok, got: "
                  << process_status.status.AsStatus() << "\n";
        abort();
      }
    }
    return true;
  }

  bool StepTime(uint64_t microseconds) { return timer_.Step(microseconds); }

 private:
  enum { kMSS = 1500 };

  class Sender final : public PacketProtocol::PacketSender {
   public:
    void SendPacket(SeqNum seq, LazySlice data, Callback<void> done) override {
      pending_sends_.emplace(next_send_id_++,
                             PendingSend{seq, std::move(data)});
    }

    struct PendingSend {
      SeqNum seq;
      LazySlice data;
    };

    Optional<PendingSend> CompleteSend(uint8_t status) {
      auto it = pending_sends_.begin();
      if (it == pending_sends_.end())
        return Nothing;
      auto ps = std::move(it->second);
      pending_sends_.erase(it);
      return std::move(ps);
    }

   private:
    std::map<uint64_t, PendingSend> pending_sends_;
    uint64_t next_send_id_ = 0;
  };

  Optional<Sender*> sender(uint8_t idx) {
    switch (idx) {
      case 1:
        return &sender1_;
      case 2:
        return &sender2_;
      default:
        return Nothing;
    }
  }

  Optional<PacketProtocol*> packet_protocol(uint8_t idx) {
    switch (idx) {
      case 1:
        return pp1_.get();
      case 2:
        return pp2_.get();
      default:
        return Nothing;
    }
  }

  bool done_ = false;
  TestTimer timer_;
  struct Logging {
    Logging(Timer* timer) : tracer(timer) {}
    TraceCout tracer;
    ScopedRenderer set_tracer{&tracer};
  };
  std::unique_ptr<Logging> logging_;
  Sender sender1_;
  Sender sender2_;
  std::mt19937 rng_{12345};
  const PacketProtocol::Codec* const codec_;
  ClosedPtr<PacketProtocol> pp1_ = MakeClosedPtr<PacketProtocol>(
      &timer_, [this] { return rng_(); }, &sender1_, codec_, kMSS);
  ClosedPtr<PacketProtocol> pp2_ = MakeClosedPtr<PacketProtocol>(
      &timer_, [this] { return rng_(); }, &sender2_, codec_, kMSS);
};  // namespace overnet

}  // namespace overnet
