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

class NubStuff {
 public:
  NubStuff(Fuzzer* fuzzer, uint8_t index);

  Router* router() { return &router_; }

 private:
  Router router_;
};

class Nub final : public NubStuff, public PacketNub<int, 256> {
 public:
  Nub(Fuzzer* fuzzer, uint8_t index);

  void SendTo(int dest, Slice slice) override;
  void Publish(LinkPtr<> link) override {
    router()->RegisterLink(std::move(link));
  }

 private:
  Fuzzer* const fuzzer_;
  const uint8_t index_;
};

class Fuzzer {
 public:
  Fuzzer();

  Timer* timer() { return &timer_; }
  bool IsDone() {
    return nub1_.router()->HasRouteTo(NodeId(2)) &&
           nub2_.router()->HasRouteTo(NodeId(1));
  }
  bool StepTime() { return timer_.StepUntilNextEvent(); }
  bool StepTime(uint64_t us) { return timer_.Step(us); }
  void QueueSend(int src, int dest, Slice slice);
  bool FlushPackets();
  void AllowPacket(uint64_t packet);
  void DropPacket(uint64_t packet);
  void Initiate1();
  void Initiate2();

  ~Fuzzer() {
    bool done1 = false;
    bool done2 = false;
    nub1_.router()->Close([&done1] { done1 = true; });
    nub2_.router()->Close([&done2] { done2 = true; });
    while (!done1 || !done2) {
      StepTime();
    }
  }

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

NubStuff::NubStuff(Fuzzer* fuzzer, uint8_t index)
    : router_(fuzzer->timer(), NodeId(index), false) {}

Nub::Nub(Fuzzer* fuzzer, uint8_t index)
    : NubStuff(fuzzer, index),
      PacketNub(router()),
      fuzzer_(fuzzer),
      index_(index) {}

void Nub::SendTo(int dest, Slice slice) {
  fuzzer_->QueueSend(index_, dest, std::move(slice));
}

Fuzzer::Fuzzer() = default;

void Fuzzer::Initiate1() { nub1_.Initiate({2}, NodeId(2)); }
void Fuzzer::Initiate2() { nub2_.Initiate({1}, NodeId(1)); }

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
  const uint8_t end_behavior = input.NextByte();
  while (!fuzzer.IsDone()) {
    switch (input.NextByte()) {
      case 0:
        OVERNET_TRACE(INFO) << "Fuzzer: Flush";
        do {
          if (input.IsEof()) {
            if (end_behavior & 1) {
              OVERNET_TRACE(INFO) << "Fuzzer: Initiate from 1 -> 2";
              fuzzer.Initiate1();
            } else {
              OVERNET_TRACE(INFO) << "Fuzzer: Initiate from 2 -> 1";
              fuzzer.Initiate2();
            }
          }
        } while (fuzzer.FlushPackets() || fuzzer.StepTime());
        if (input.IsEof() && !fuzzer.IsDone()) {
          std::cerr << "Failed to connect\n";
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
      case 4: {
        OVERNET_TRACE(INFO) << "Fuzzer: Initiate from 1 -> 2";
        fuzzer.Initiate1();
      } break;
      case 5: {
        OVERNET_TRACE(INFO) << "Fuzzer: Initiate from 2 -> 1";
        fuzzer.Initiate2();
      } break;
      default: {
        return 0;
      }
    }
  }
  return 0;
}
