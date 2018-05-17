// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitset>
#include <deque>
#include "ack_frame.h"
#include "bbr.h"
#include "callback.h"
#include "optional.h"
#include "seq_num.h"
#include "slice.h"
#include "status.h"
#include "timer.h"
#include "varint.h"

namespace overnet {

class PacketProtocol {
 public:
  class PacketSender {
   public:
    virtual void SendPacket(SeqNum seq, Slice data, StatusCallback done) = 0;
  };

  static constexpr size_t kLookaheadWindow = 256;
  static constexpr size_t kMaxUnackedReceives = 3;

  PacketProtocol(Timer* timer, PacketSender* packet_sender, uint64_t mss)
      : timer_(timer),
        packet_sender_(packet_sender),
        mss_(mss),
        outgoing_bbr_(timer_, mss_, Nothing) {}

  using SendCallback = Callback<Status, 16 * sizeof(void*)>;

  struct SendData {
    Slice slice;
    StatusCallback sent;
    SendCallback acked;
  };

  template <
      class F /* F: (uint64_t desire_prefix, uint64_t max_len) -> SendData */>
  void Send(F generate) {
    auto ack = GenerateAck();
    if (ack) {
      AckFrame::Writer ack_writer(ack.get());
      const uint8_t ack_length_length =
          varint::WireSizeFor(ack_writer.wire_length());
      const uint64_t prefix_length =
          ack_length_length + ack_writer.wire_length();
      SendData send_data = generate(prefix_length, mss_ - prefix_length);
      MaybeSendSlice(QueuedPacket{
          prefix_length,
          send_data.slice.WithPrefix(
              prefix_length,
              [&ack_writer, ack_length_length](uint8_t* p) {
                ack_writer.Write(varint::Write(ack_writer.wire_length(),
                                               ack_length_length, p));
              }),
          std::move(send_data.sent), std::move(send_data.acked)});
    } else {
      SendData send_data = generate(1, mss_ - 1);
      MaybeSendSlice(QueuedPacket{
          1, send_data.slice.WithPrefix(1, [](uint8_t* p) { *p = 0; }),
          std::move(send_data.sent), std::move(send_data.acked)});
    }
  }

  StatusOr<Optional<Slice>> Process(TimeStamp received, SeqNum seq,
                                    Slice slice);

 private:
  struct OutstandingPacket {
    uint64_t ack_to_seq;
    BBR::SentPacket bbr_sent_packet;
    SendCallback finished;
  };

  struct QueuedPacket {
    size_t payload_offset;
    Slice send_packet;
    StatusCallback sent;
    SendCallback finished;
  };

  void MaybeForceAck();
  void MaybeScheduleAck();
  void MaybeSendAck();
  void MaybeSendSlice(QueuedPacket&& packet);
  void SendSlice(QueuedPacket&& packet);
  Status HandleAck(const AckFrame& ack);
  void ContinueSending();

  Optional<AckFrame> GenerateAck();

  Timer* const timer_;
  PacketSender* const packet_sender_;
  const uint64_t mss_;

  BBR outgoing_bbr_;
  // Store ack data on object to avoid vector reallocations.
  BBR::Ack bbr_ack_;

  // TODO(ctiller): can we move to a ring buffer here? - the idea would be to
  // just finished(RESOURCE_EXHAUSTED) if the ring is full
  uint64_t send_tip_ = 1;
  std::deque<OutstandingPacket> outstanding_;
  std::deque<QueuedPacket> queued_;
  Optional<QueuedPacket> sending_;

  uint64_t recv_tip_ = 1;
  uint64_t max_seen_ = 0;
  TimeStamp max_seen_time_ = TimeStamp::Epoch();
  uint64_t max_acked_ = 0;
  std::bitset<kLookaheadWindow> received_;
  std::bitset<kLookaheadWindow> frozen_;

  Optional<Timeout> ack_scheduler_;
};

}  // namespace overnet
