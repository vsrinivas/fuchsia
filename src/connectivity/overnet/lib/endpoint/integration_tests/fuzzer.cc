// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/endpoint/integration_tests/tests.h"
#include "src/connectivity/overnet/lib/links/packet_link.h"
#include "src/connectivity/overnet/lib/links/stream_link.h"
#include "src/connectivity/overnet/lib/protocol/unreliable_framer.h"

using namespace overnet;
using namespace overnet::endpoint_integration_tests;

namespace {

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

 private:
  const uint8_t* cur_;
  const uint8_t* end_;
};

template <class T>
T Clamp(T a, T min, T max) {
  if (a < min) {
    return min;
  } else if (a > max) {
    return max;
  } else {
    return a;
  }
}

class FuzzingPacketLink final
    : public PacketLink,
      public std::enable_shared_from_this<FuzzingPacketLink> {
 public:
  FuzzingPacketLink(RouterEndpoint* src, RouterEndpoint* dest, uint64_t link_id,
                    InputStream* input)
      : PacketLink(src, dest->node_id(),
                   Clamp(input->Next64(), uint64_t(256), uint64_t(65536)),
                   link_id),
        timer_(dest->timer()),
        input_(input),
        from_(src->node_id()) {
    OVERNET_TRACE(INFO) << "NEW PACKET LINK " << link_id
                        << " mss=" << GetLinkStatus().metrics.mss();
    src->RegisterPeer(dest->node_id());
  }

  ~FuzzingPacketLink() {
    auto strong_partner = partner_.lock();
    if (strong_partner != nullptr) {
      strong_partner->partner_.reset();
    }
  }

  void Partner(std::shared_ptr<FuzzingPacketLink> other) {
    partner_ = other;
    other->partner_ = weak_from_this();
  }

  void Emit(Slice packet) {
    const auto now = timer_->Now();
    if (now.after_epoch() == TimeDelta::PositiveInf()) {
      OVERNET_TRACE(DEBUG)
          << "Packet sim is infinitely in the future: drop packet";
      return;
    }

    // Add one so that at end of stream we always guarantee one delivery.
    int delivery_count = static_cast<uint8_t>(1 + input_->NextByte());
    OVERNET_TRACE(DEBUG) << "ScheduleEmit " << delivery_count
                         << " times: " << packet;
    for (int i = 0; i < delivery_count; i++) {
      const auto at = now + TimeDelta::FromMicroseconds(input_->Next64());
      OVERNET_TRACE(DEBUG) << "  at: " << at;
      timer_->At(
          at, Callback<void>(ALLOCATED_CALLBACK, [weak_self = weak_from_this(),
                                                  packet, at]() {
            OVERNET_TRACE(DEBUG) << "Emit: " << packet;
            ScopedOp scoped_op(Op::New(OpType::INCOMING_PACKET));
            auto self = weak_self.lock();
            if (!self) {
              return;
            }
            auto strong_partner = self->partner_.lock();
            if (strong_partner) {
              strong_partner->Process(at, packet);
            }
          }));
    }
  }

 private:
  Timer* const timer_;
  InputStream* const input_;
  std::weak_ptr<FuzzingPacketLink> partner_;
  const NodeId from_;
};

class FuzzingUnreliableStreamLink final
    : public StreamLink,
      public std::enable_shared_from_this<FuzzingUnreliableStreamLink> {
 public:
  FuzzingUnreliableStreamLink(RouterEndpoint* src, RouterEndpoint* dest,
                              uint64_t link_id, InputStream* input)
      : StreamLink(src, dest->node_id(), std::make_unique<UnreliableFramer>(),
                   link_id),
        timer_(src->timer()),
        input_(input) {}

  ~FuzzingUnreliableStreamLink() {
    auto strong_partner = partner_.lock();
    if (strong_partner != nullptr) {
      strong_partner->partner_.reset();
    }
  }

  void Partner(std::shared_ptr<FuzzingUnreliableStreamLink> other) {
    partner_ = other;
    other->partner_ = weak_from_this();
    // Late initialization...
    SchedAddNoise();
    other->SchedAddNoise();
  }

  void Close(Callback<void> quiesced) override {
    OVERNET_TRACE(DEBUG) << this << " Close";
    {
      auto strong_partner = partner_.lock();
      if (strong_partner != nullptr) {
        strong_partner->partner_.reset();
        if (!strong_partner->done_.empty()) {
          strong_partner->done_(Status::Cancelled());
        }
      }
      partner_.reset();
      if (!done_.empty()) {
        done_(Status::Cancelled());
      }
    }
    StreamLink::Close(std::move(quiesced));
  }

  void Emit(Slice bytes, StatusCallback done) override {
    auto done_byte = input_->Next64();
    assert(done_.empty());
    done_ = std::move(done);
    bool captured_done = false;
    for (uint8_t b : bytes) {
      auto when = std::max(last_output_, timer_->Now()) + time_per_byte_;
      last_output_ = when;
      auto cb = Callback<void>::Ignored();
      if (done_byte-- == 0) {
        captured_done = true;
        OVERNET_TRACE(DEBUG) << "capture done";
        cb = Callback<void>(ALLOCATED_CALLBACK,
                            [weak_self = weak_from_this()]() {
                              auto self = weak_self.lock();
                              if (!self) {
                                return;
                              }
                              if (!self->done_.empty()) {
                                self->done_(Status::Ok());
                              }
                            });
      }
      SchedByte(when, b, std::move(cb));
    }
    if (!captured_done) {
      done_(Status::Ok());
    }
  }

 private:
  void SchedByte(TimeStamp when, uint8_t b, Callback<void> then) {
    OVERNET_TRACE(DEBUG) << this << " SchedByte " << when << " "
                         << static_cast<int>(b);
    timer_->At(
        when,
        Callback<void>(ALLOCATED_CALLBACK, [weak_self = weak_from_this(), b,
                                            when, then = std::move(then),
                                            debug_self_ptr = this]() mutable {
          OVERNET_TRACE(DEBUG) << debug_self_ptr << " SchedByte ready " << when
                               << " " << static_cast<int>(b);
          auto self = weak_self.lock();
          if (!self) {
            return;
          }
          auto strong_partner = self->partner_.lock();
          if (0 == --self->bytes_until_mutation_) {
            b ^= self->input_->NextByte();
            self->bytes_until_mutation_ = self->input_->Next64();
          }
          if (strong_partner) {
            strong_partner->Process(when, Slice::RepeatedChar(1, b));
          }
        }));
  }

  void SchedAddNoise() {
    auto delay = input_->Next64();
    if (delay == 0) {
      return;
    }
    SchedByte(
        timer_->Now() + TimeDelta::FromMicroseconds(delay), input_->NextByte(),
        Callback<void>(ALLOCATED_CALLBACK, [weak_self = weak_from_this()] {
          auto self = weak_self.lock();
          if (!self) {
            return;
          }
          self->SchedAddNoise();
        }));
  }

  Timer* const timer_;
  InputStream* const input_;
  TimeStamp last_output_ = TimeStamp::Epoch();
  uint64_t bytes_until_mutation_ = input_->Next64();
  const TimeDelta time_per_byte_ =
      Clamp(TimeDelta::FromMicroseconds(input_->Next64()),
            TimeDelta::FromMicroseconds(1), TimeDelta::FromMilliseconds(1));
  std::weak_ptr<FuzzingUnreliableStreamLink> partner_;
  StatusCallback done_;
};

enum class LinkType : uint8_t {
  Packet,
  UnreliableStream,
};

static bool IsValidLinkType(LinkType l) {
  switch (l) {
    case LinkType::Packet:
    case LinkType::UnreliableStream:
      return true;
  }
  return false;
}

class FuzzingSimulator final : public Simulator {
 public:
  FuzzingSimulator(InputStream* input, LinkType link_type)
      : link_type_(link_type), input_(input) {}

  void MakeLinks(RouterEndpoint* ep1, RouterEndpoint* ep2, uint64_t id1,
                 uint64_t id2) const override {
    switch (link_type_) {
      case LinkType::Packet:
        MakeLinksWithImpl<FuzzingPacketLink>(ep1, ep2, id1, id2);
        return;
      case LinkType::UnreliableStream:
        MakeLinksWithImpl<FuzzingUnreliableStreamLink>(ep1, ep2, id1, id2);
        return;
    }
    abort();
  }

 private:
  LinkType link_type_;
  InputStream* const input_;

  template <class LinkImpl>
  void MakeLinksWithImpl(RouterEndpoint* ep1, RouterEndpoint* ep2, uint64_t id1,
                         uint64_t id2) const {
    auto link1 = MakeLink<InProcessLink<LinkImpl>>(ep1, ep2, id1, input_);
    auto link2 = MakeLink<InProcessLink<LinkImpl>>(ep2, ep1, id2, input_);
    link1->get()->Partner(link2->get());
    ep1->RegisterLink(std::move(link1));
    ep2->RegisterLink(std::move(link2));
  }
};

}  // namespace

#ifdef FUZZER_REPLAY
const Optional<Severity> g_logging = Severity::DEBUG;
#else
const Optional<Severity> g_logging = Nothing;
#endif

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InputStream input(data, size);
  auto link_type = static_cast<LinkType>(input.NextByte());
  if (!IsValidLinkType(link_type)) {
    return 0;
  }
  const NamedSimulator kSimulator{
      "fuzzing_simulator",
      std::make_unique<FuzzingSimulator>(&input, link_type)};
  auto node1 = input.NextByte();
  auto node2 = input.NextByte();
  if (node2 == node1) {
    return 0;
  }
  TwoNode env(g_logging, &kSimulator, node1, node2);
  switch (input.NextByte()) {
    default:
      return 0;
    case 1: {
      auto length = Clamp(input.Next64(), uint64_t(1), uint64_t(65536));
      OVERNET_TRACE(DEBUG) << "Length: " << length;
      OneMessageSrcToDest(&env, Slice::RepeatedChar(length, 'f'), Nothing);
      return 0;
    }
    case 2: {
      auto length1 = Clamp(input.Next64(), uint64_t(1), uint64_t(65536));
      auto length2 = Clamp(input.Next64(), uint64_t(1), uint64_t(65536));
      OVERNET_TRACE(DEBUG) << "Length1: " << length1;
      OVERNET_TRACE(DEBUG) << "Length2: " << length2;
      RequestResponse(&env, Slice::RepeatedChar(length1, 'r'),
                      Slice::RepeatedChar(length2, 'R'), Nothing);
      return 0;
    }
  }
}

#ifdef FUZZER_REPLAY
int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    FILE* f = fopen(argv[i], "r");
    ZX_ASSERT(f);
    fseek(f, 0, SEEK_END);
    auto len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::unique_ptr<uint8_t[]> buf(new uint8_t[len]);
    ZX_ASSERT(size_t(len) == fread(buf.get(), 1, len, f));
    fclose(f);
    LLVMFuzzerTestOneInput(buf.get(), len);
  }
}
#endif
