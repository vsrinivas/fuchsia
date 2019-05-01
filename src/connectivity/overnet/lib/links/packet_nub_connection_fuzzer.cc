// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This fuzzer tests the hypothesis that the packet nub protocol allows
// eventual connectivity even in the face of packet loss.

#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/links/packet_nub.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"

using namespace overnet;

///////////////////////////////////////////////////////////////////////////////
// Simulation

class Fuzzer;

class Nub final : public PacketNub<int, 256> {
 public:
  Nub(Fuzzer* fuzzer, uint8_t index);

  void SendTo(int dest, Slice slice) override;
  Router* GetRouter() override;
  void Publish(LinkPtr<> link) override {
    done_ = true;
    router_.RegisterLink(std::move(link));
  }

  bool IsDone() const { return done_; }

 private:
  Fuzzer* const fuzzer_;
  const uint8_t index_;
  Router router_;
  bool done_ = false;
};

class Fuzzer {
 public:
  Fuzzer();

  Timer* timer() { return &timer_; }
  bool IsDone() const { return nub2_.IsDone(); }
  bool StepTime() { return timer_.StepUntilNextEvent(); }
  bool StepTime(uint64_t us) { return timer_.Step(us); }
  void QueueSend(int src, int dest, Slice slice);
  bool FlushPackets();
  void AllowPacket(uint64_t packet);
  void DropPacket(uint64_t packet);
  void BeginStep();

 private:
  TestTimer timer_;
  TraceCout cout_{&timer_};
  // ScopedRenderer renderer_{&cout_};
  Nub nub1_{this, 1};
  Nub nub2_{this, 2};

  enum class PacketState : uint8_t {
    QUEUED,
    SENT,
    DROPPED,
  };
  struct Packet {
    int src;
    int dest;
    PacketState state;
    Slice payload;
  };
  std::vector<Packet> packets_;
};

Nub::Nub(Fuzzer* fuzzer, uint8_t index)
    : PacketNub(fuzzer->timer(), NodeId(index)),
      fuzzer_(fuzzer),
      index_(index),
      router_(fuzzer->timer(), NodeId(index), false) {}

void Nub::SendTo(int dest, Slice slice) {
  fuzzer_->QueueSend(index_, dest, std::move(slice));
}

Router* Nub::GetRouter() { return &router_; }

Fuzzer::Fuzzer() = default;

void Fuzzer::BeginStep() { nub1_.Initiate(2, NodeId(2)); }

void Fuzzer::QueueSend(int src, int dest, Slice slice) {
  packets_.emplace_back(
      Packet{src, dest, PacketState::QUEUED, std::move(slice)});
}

bool Fuzzer::FlushPackets() {
  bool flushed_any = false;
  for (size_t i = 0; i < packets_.size(); i++) {
    if (packets_[i].state == PacketState::QUEUED) {
      AllowPacket(i);
      assert(packets_[i].state != PacketState::QUEUED);
      flushed_any = true;
    }
  }
  return flushed_any;
}

void Fuzzer::AllowPacket(uint64_t packet) {
  if (packet >= packets_.size()) {
    return;
  }
  auto& p = packets_[packet];
  if (p.state == PacketState::DROPPED) {
    return;
  }
  p.state = PacketState::SENT;
  const auto now = timer_.Now();
  switch (p.dest) {
    case 1:
      nub1_.Process(now, p.src, p.payload);
      break;
    case 2:
      nub2_.Process(now, p.src, p.payload);
      break;
  }
}

void Fuzzer::DropPacket(uint64_t packet) {
  if (packet >= packets_.size()) {
    return;
  }
  auto& p = packets_[packet];
  p.state = PacketState::DROPPED;
}

///////////////////////////////////////////////////////////////////////////////
// Input

class InputStream {
 public:
  InputStream(const uint8_t* data, size_t size)
      : cur_(data), end_(data + size) {}

  uint64_t Next64() {
    uint64_t out;
    if (!varint::Read(&cur_, end_, &out))
      out = 0;
    return out;
  }

  uint8_t NextByte() {
    if (cur_ == end_)
      return 0;
    return *cur_++;
  }

  bool IsEof() const { return cur_ == end_; }

 private:
  const uint8_t* cur_;
  const uint8_t* end_;
};

///////////////////////////////////////////////////////////////////////////////
// Main loop

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InputStream input(data, size);
  Fuzzer fuzzer;
  while (!fuzzer.IsDone()) {
    fuzzer.BeginStep();
    switch (input.NextByte()) {
      case 0:
        OVERNET_TRACE(INFO) << "Fuzzer: Flush";
        do {
          fuzzer.BeginStep();
        } while (fuzzer.FlushPackets() || fuzzer.StepTime());
        if (input.IsEof() && !fuzzer.IsDone()) {
          abort();
        }
        break;
      case 1: {
        auto dt = TimeDelta::FromMicroseconds(input.Next64());
        dt = std::min(dt, TimeDelta::FromHours(1));
        dt = std::max(dt, TimeDelta::FromMicroseconds(1));
        OVERNET_TRACE(INFO) << "Fuzzer: Step " << dt;
        fuzzer.StepTime(dt.as_us());
      } break;
      case 2: {
        auto idx = input.Next64();
        OVERNET_TRACE(INFO) << "Fuzzer: Allow packet " << idx;
        fuzzer.AllowPacket(idx);
      } break;
      case 3: {
        auto idx = input.Next64();
        OVERNET_TRACE(INFO) << "Fuzzer: Drop packet " << idx;
        fuzzer.DropPacket(idx);
      } break;
      default: {
        return 0;
      }
    }
  }
  return 0;
}
