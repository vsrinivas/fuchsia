// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iostream>
#include <map>
#include "packet_protocol.h"
#include "test_timer.h"

namespace overnet {

class PacketProtocolFuzzer {
 public:
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

  bool CompleteSend(uint8_t sender_idx, uint64_t send_idx, uint8_t status) {
    Optional<Sender::PendingSend> send =
        sender(sender_idx).Then([send_idx, status](Sender* sender) {
          return sender->CompleteSend(send_idx, status);
        });
    if (!send)
      return false;
    if (status == 0) {
      auto now = timer_.Now();
      auto when = now;
      auto slice = send->data(
          LazySliceArgs{0, std::numeric_limits<uint32_t>::max(), false, &when});
      timer_.At(when, [=, seq = send->seq] {
        auto process_status = (*packet_protocol(3 - sender_idx))
                                  ->Process(timer_.Now(), seq, slice);
        if (process_status.status.is_error()) {
          std::cerr << "Expected Process() to return ok, got: "
                    << process_status.status.AsStatus() << "\n";
          abort();
        }
      });
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

    Optional<PendingSend> CompleteSend(uint64_t send_idx, uint8_t status) {
      auto it = pending_sends_.find(send_idx);
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
  Sender sender1_;
  Sender sender2_;
  ClosedPtr<PacketProtocol> pp1_ =
      MakeClosedPtr<PacketProtocol>(&timer_, &sender1_, TraceSink(), kMSS);
  ClosedPtr<PacketProtocol> pp2_ =
      MakeClosedPtr<PacketProtocol>(&timer_, &sender2_, TraceSink(), kMSS);
};  // namespace overnet

}  // namespace overnet
