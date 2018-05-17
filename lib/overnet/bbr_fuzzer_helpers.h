// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include "bbr.h"
#include "test_timer.h"

#pragma once

namespace overnet {

class BBRFuzzer {
 public:
  // Cap time increments to one year
  static constexpr uint64_t kMaxTimeDelta = 365 * 24 * 3600 * kUsPerSec;

  BBRFuzzer(uint64_t start_time, uint64_t mss, uint64_t srtt)
      : timer_(CapTimeDelta(start_time)),
        bbr_(timer_.Now(),
             std::max(uint64_t(1), std::min(uint64_t(BBR::kMaxMSS), mss)),
             srtt ? Optional<TimeDelta>(TimeDelta::FromMicroseconds(srtt))
                  : Nothing) {}

  void IncTime(uint64_t us) { timer_.Step(CapTimeDelta(us)); }

  void Transmit(uint64_t size, uint64_t send_delay) {
    send_queue_.emplace(std::piecewise_construct,
                        std::forward_as_tuple(next_seq_),
                        std::forward_as_tuple(this, std::min(bbr_.mss(), size),
                                              CapTimeDelta(send_delay)));
    next_seq_++;
  }

  void Ack(std::vector<uint64_t> acks, std::vector<uint64_t> nacks) {
    std::vector<BBR::SentPacket> ack_packets;
    std::vector<BBR::SentPacket> nack_packets;
    std::sort(acks.begin(), acks.end());
    std::sort(nacks.begin(), nacks.end());
    for (auto seq : acks) {
      auto it = sent_packets_.find(seq);
      if (it == sent_packets_.end()) continue;
      ack_packets.push_back(it->second);
      sent_packets_.erase(it);
    }
    for (auto seq : nacks) {
      auto it = sent_packets_.find(seq);
      if (it == sent_packets_.end()) continue;
      nack_packets.push_back(it->second);
      sent_packets_.erase(it);
    }
    bbr_.OnAck(BBR::Ack{timer_.Now(), ack_packets, nack_packets});
  }

 private:
  TestTimer timer_;
  BBR bbr_;
  uint64_t next_seq_ = 1;

  static uint64_t CapTimeDelta(uint64_t t) {
    if (t > kMaxTimeDelta) return kMaxTimeDelta;
    return t;
  }

  class SendElem {
   public:
    SendElem(BBRFuzzer* fuzzer, uint64_t size, uint64_t send_delay)
        : packet_(fuzzer->bbr_.OnTransmit(
              BBR::ExternalState{fuzzer->timer_.Now(),
                                 fuzzer->send_queue_.size()},
              BBR::OutgoingPacket{fuzzer->next_seq_, size})),
          send_timeout_(
              &fuzzer->timer_,
              fuzzer->timer_.Now() + TimeDelta::FromMicroseconds(send_delay),
              StatusCallback(
                  ALLOCATED_CALLBACK, [this, fuzzer](const Status& status) {
                    if (status.is_ok()) {
                      fuzzer->sent_packets_.emplace(packet_.outgoing.sequence,
                                                    packet_);
                      fuzzer->send_queue_.erase(packet_.outgoing.sequence);
                    }
                  })) {}

   private:
    BBR::SentPacket packet_;
    Timeout send_timeout_;
  };

  std::map<uint64_t, SendElem> send_queue_;
  std::map<uint64_t, BBR::SentPacket> sent_packets_;
};

}  // namespace overnet
