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
      pp->Send([this, data](uint64_t desire_prefix, uint64_t max_len) {
        return PacketProtocol::SendData{
            data, StatusCallback::Ignored(), [this](const Status& status) {
              if (done_) return;
              if (!status.is_ok() && status.code() != StatusCode::CANCELLED) {
                std::cerr << "Expected each send to be ok or cancelled, got: "
                          << status << "\n";
                abort();
              }
            }};
      });
      return true;
    });
  }

  bool CompleteSend(uint8_t sender_idx, uint64_t send_idx, uint8_t status) {
    Optional<Sender::PendingSend> send =
        sender(sender_idx).Then([send_idx, status](Sender* sender) {
          return sender->CompleteSend(send_idx, status);
        });
    if (!send) return false;
    send->done(Status(static_cast<StatusCode>(status)));
    auto process_status =
        status == 0 ? (*packet_protocol(3 - sender_idx))
                          ->Process(timer_.Now(), send->seq, send->data)
                    : Nothing;
    if (process_status.is_error()) {
      std::cerr << "Expected Processs() to return ok, got: " << process_status
                << "\n";
      abort();
    }
    return true;
  }

  bool StepTime(uint64_t microseconds) { return timer_.Step(microseconds); }

 private:
  static const uint64_t kMSS = 1500;

  class Sender : public PacketProtocol::PacketSender {
   public:
    void SendPacket(SeqNum seq, Slice data, StatusCallback done) {
      pending_sends_.emplace(
          next_send_id_++, PendingSend{seq, std::move(data), std::move(done)});
    }

    struct PendingSend {
      SeqNum seq;
      Slice data;
      StatusCallback done;
    };

    Optional<PendingSend> CompleteSend(uint64_t send_idx, uint8_t status) {
      auto it = pending_sends_.find(send_idx);
      if (it == pending_sends_.end()) return Nothing;
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
        return &pp1_;
      case 2:
        return &pp2_;
      default:
        return Nothing;
    }
  }

  bool done_ = false;
  TestTimer timer_;
  Sender sender1_;
  Sender sender2_;
  PacketProtocol pp1_{&timer_, &sender1_, kMSS};
  PacketProtocol pp2_{&timer_, &sender2_, kMSS};
};

}  // namespace overnet
