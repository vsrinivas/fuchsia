// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <tuple>

#include "gtest/gtest.h"
#include "src/connectivity/overnet/lib/endpoint/integration_tests/tests.h"
#include "src/connectivity/overnet/lib/links/packet_link.h"
#include "src/connectivity/overnet/lib/links/stream_link.h"
#include "src/connectivity/overnet/lib/protocol/reliable_framer.h"
#include "src/connectivity/overnet/lib/protocol/unreliable_framer.h"
#include "src/connectivity/overnet/lib/testing/flags.h"

//////////////////////////////////////////////////////////////////////////////
// Two node fling

namespace overnet {
namespace endpoint_integration_tests {

struct LinkState {
  uint64_t id;
  int outstanding_packets;
};

class PacketPacer {
 public:
  virtual ~PacketPacer() {}
  // Returns Nothing if the slice should be dropped, or a delay if it should be
  // delivered.
  virtual Optional<TimeDelta> ChoosePacketDelivery(LinkState link_state,
                                                   size_t slice_size) const = 0;
  virtual std::string name() const = 0;
  virtual Bandwidth SimulatedBandwidth() const = 0;
  virtual uint32_t MaximumSegmentSize() const = 0;
};

// Very fast reliable packet delivery: it's often easier to debug problems
// with HappyDelivery than with packet loss enabled (assuming it shows up).
class HappyDelivery : public PacketPacer {
 public:
  HappyDelivery(uint32_t mss) : mss_(mss) {}

  Optional<TimeDelta> ChoosePacketDelivery(LinkState link_state,
                                           size_t slice_size) const override {
    return TimeDelta::FromMicroseconds(1);
  }

  std::string name() const override {
    return "HappyDelivery(" + std::to_string(mss_) + ")";
  }

  Bandwidth SimulatedBandwidth() const override {
    return Bandwidth::FromKilobitsPerSecond(1024 * 1024);
  }

  virtual uint32_t MaximumSegmentSize() const override { return mss_; }

 private:
  const uint32_t mss_;
};

// Windowed number of outstanding packets
class WindowedDelivery : public PacketPacer {
 public:
  WindowedDelivery(int max_outstanding, TimeDelta window, uint32_t mss)
      : max_outstanding_(max_outstanding), window_(window), mss_(mss) {}

  Optional<TimeDelta> ChoosePacketDelivery(LinkState link_state,
                                           size_t slice_size) const override {
    if (link_state.outstanding_packets >= max_outstanding_)
      return Nothing;
    return window_;
  }

  std::string name() const override {
    std::ostringstream out;
    out << "WindowedDelivery(" << max_outstanding_
        << ", TimeDelta::FromMicroseconds(" << window_.as_us() << "), " << mss_
        << ")";
    return out.str();
  }

  Bandwidth SimulatedBandwidth() const override {
    return Bandwidth::BytesPerTime(MaximumSegmentSize() * max_outstanding_,
                                   window_);
  }

  virtual uint32_t MaximumSegmentSize() const override { return mss_; }

 private:
  const int max_outstanding_;
  const TimeDelta window_;
  const uint32_t mss_;
};

class InProcessPacketLinkImpl final
    : public PacketLink,
      public std::enable_shared_from_this<InProcessPacketLinkImpl> {
 public:
  InProcessPacketLinkImpl(RouterEndpoint* src, RouterEndpoint* dest,
                          uint64_t link_id, const PacketPacer* simulator)
      : PacketLink(src, dest->node_id(), simulator->MaximumSegmentSize(),
                   link_id),
        timer_(dest->timer()),
        link_id_(link_id),
        simulator_(simulator),
        from_(src->node_id()) {
    src->RegisterPeer(dest->node_id());
  }

  ~InProcessPacketLinkImpl() {
    auto strong_partner = partner_.lock();
    if (strong_partner != nullptr) {
      strong_partner->partner_.reset();
    }
  }

  void Partner(std::shared_ptr<InProcessPacketLinkImpl> other) {
    partner_ = other;
    other->partner_ = shared_from_this();
  }

  void Emit(Slice packet) {
    const auto now = timer_->Now();
    if (now.after_epoch() == TimeDelta::PositiveInf()) {
      OVERNET_TRACE(DEBUG)
          << "Packet sim is infinitely in the future: drop packet";
      return;
    }

    auto delay = simulator_->ChoosePacketDelivery(
        LinkState{link_id_, outstanding_packets_}, packet.length());
    OVERNET_TRACE(DEBUG) << "Packet sim says " << delay << " for " << packet;
    if (!delay.has_value()) {
      return;
    }
    outstanding_packets_++;
    const auto at = now + *delay;
    timer_->At(
        at, Callback<void>(
                ALLOCATED_CALLBACK, [self = shared_from_this(), packet, at]() {
                  ScopedOp scoped_op(Op::New(OpType::INCOMING_PACKET));
                  auto strong_partner = self->partner_.lock();
                  OVERNET_TRACE(DEBUG)
                      << (strong_partner == nullptr ? "DROP" : "EMIT")
                      << " PACKET from " << self->from_ << " " << packet;
                  self->outstanding_packets_--;
                  if (strong_partner) {
                    strong_partner->Process(at, packet);
                  }
                }));
  }

 private:
  Timer* const timer_;
  const uint64_t link_id_;
  const PacketPacer* const simulator_;
  std::weak_ptr<InProcessPacketLinkImpl> partner_;
  const NodeId from_;
  int outstanding_packets_ = 0;
};

template <class Framer>
class InProcessStreamLinkImpl final
    : public StreamLink,
      public std::enable_shared_from_this<InProcessStreamLinkImpl<Framer>> {
 public:
  InProcessStreamLinkImpl(RouterEndpoint* src, RouterEndpoint* dest,
                          uint64_t link_id, Bandwidth bandwidth)
      : StreamLink(src, dest->node_id(), std::make_unique<Framer>(), link_id),
        timer_(src->timer()),
        bandwidth_(bandwidth),
        from_(src->node_id()) {
    src->RegisterPeer(dest->node_id());
  }

  void Partner(std::shared_ptr<InProcessStreamLinkImpl> other) {
    partner_ = other;
    other->partner_ = this->shared_from_this();
  }

  void Emit(Slice slice, StatusCallback done) override {
    OVERNET_TRACE(DEBUG) << "Emit: " << slice;

    assert(!send_op_.has_value());
    send_op_.Reset(SendOp{std::move(slice), std::move(done)});

    SendNext();
  }

 private:
  void SendNext() {
    auto max_delivery =
        std::max(uint64_t(1),
                 bandwidth_.BytesSentForTime(TimeDelta::FromMicroseconds(100)));
    Slice chunk;
    StatusCallback cb;
    if (max_delivery >= send_op_->slice.length()) {
      chunk = std::move(send_op_->slice);
      cb = std::move(send_op_->done);
      send_op_.Reset();
    } else {
      chunk = send_op_->slice.TakeUntilOffset(max_delivery);
    }

    auto now = timer_->Now();
    next_send_completes_ = std::max(now, next_send_completes_) +
                           bandwidth_.SendTimeForBytes(chunk.length());
    timer_->At(next_send_completes_, [chunk = std::move(chunk),
                                      at = next_send_completes_,
                                      self = this->shared_from_this()] {
      auto strong_partner = self->partner_.lock();
      OVERNET_TRACE(DEBUG) << (strong_partner == nullptr ? "DROP" : "EMIT")
                           << " BYTES from " << self->from_ << " " << chunk;
      if (strong_partner) {
        strong_partner->Process(at, chunk);
      }
    });

    if (!cb.empty()) {
      cb(Status::Ok());
    } else {
      SendNext();
    }
  }

  Timer* const timer_;
  std::weak_ptr<InProcessStreamLinkImpl> partner_;
  const Bandwidth bandwidth_;
  const NodeId from_;
  struct SendOp {
    Slice slice;
    Callback<Status> done;
  };
  Optional<SendOp> send_op_;
  TimeStamp next_send_completes_ = TimeStamp::Epoch();
};

class PacketLinkSimulator final : public Simulator {
 public:
  PacketLinkSimulator(std::unique_ptr<PacketPacer> pacer)
      : pacer_(std::move(pacer)) {}

  void MakeLinks(RouterEndpoint* ep1, RouterEndpoint* ep2, uint64_t id1,
                 uint64_t id2) const override {
    auto link1 = MakeLink<InProcessLink<InProcessPacketLinkImpl>>(ep1, ep2, id1,
                                                                  pacer_.get());
    auto link2 = MakeLink<InProcessLink<InProcessPacketLinkImpl>>(ep2, ep1, id2,
                                                                  pacer_.get());
    link1->get()->Partner(link2->get());
    ep1->RegisterLink(std::move(link1));
    ep2->RegisterLink(std::move(link2));
  }

 private:
  std::unique_ptr<PacketPacer> pacer_;
};

const char* FramerName(const ReliableFramer& framer) {
  return "ReliableFramer";
};

const char* FramerName(const UnreliableFramer& framer) {
  return "UnreliableFramer";
};

template <class Framer>
class StreamLinkSimulator final : public Simulator {
 public:
  StreamLinkSimulator(Bandwidth bandwidth) : bandwidth_(bandwidth) {}

  void MakeLinks(RouterEndpoint* ep1, RouterEndpoint* ep2, uint64_t id1,
                 uint64_t id2) const override {
    auto link1 = MakeLink<InProcessLink<InProcessStreamLinkImpl<Framer>>>(
        ep1, ep2, id1, bandwidth_);
    auto link2 = MakeLink<InProcessLink<InProcessStreamLinkImpl<Framer>>>(
        ep2, ep1, id2, bandwidth_);
    link1->get()->Partner(link2->get());
    ep1->RegisterLink(std::move(link1));
    ep2->RegisterLink(std::move(link2));
  }

 private:
  const Bandwidth bandwidth_;
};

class RouterEndpoint_IntegrationEnv : public ::testing::TestWithParam<MakeEnv> {
};

std::ostream& operator<<(std::ostream& out, MakeEnv env) {
  return out << env->name();
}

Optional<Severity> Logging() {
  return FLAGS_verbose ? Severity::DEBUG : Severity::INFO;
}

TEST_P(RouterEndpoint_IntegrationEnv, NoOp) {
  std::cout << "Param: " << GetParam() << std::endl;
  NoOpTest(GetParam()->Make(Logging()).get());
}

TEST_P(RouterEndpoint_IntegrationEnv, NodeDescriptionPropagation) {
  std::cout << "Param: " << GetParam() << std::endl;
  NodeDescriptionPropagationTest(GetParam()->Make(Logging()).get());
}

template <class T>
std::string ChangeArg(std::string input, int arg, T value) {
  std::ostringstream out;
  for (int i = 0; i < arg - 1; i++) {
    auto cpos = input.find(',');
    assert(cpos != std::string::npos);
    out << input.substr(0, cpos + 1);
    input = input.substr(cpos + 1);
  }
  out << ' ' << value;
  out << input.substr(input.find(','));
  return out.str();
}

std::string EscapeChars(std::string chars, std::string input) {
  std::string output;
  for (auto c : input) {
    if (chars.find(c) != std::string::npos) {
      output += '\\';
    }
    output += c;
  }
  return output;
}

struct OneMessageArgs {
  MakeEnv make_env;
  Slice body;
  uint32_t allowed_time;
  uint64_t expected_packets;
};

std::ostream& operator<<(std::ostream& out, OneMessageArgs args) {
  return out << args.make_env->name();
}

class RouterEndpoint_OneMessageIntegration
    : public ::testing::TestWithParam<OneMessageArgs> {};

TEST_P(RouterEndpoint_OneMessageIntegration, Works) {
  std::cout << "Param: " << GetParam() << std::endl;
  auto env = GetParam().make_env->Make(Logging());

  auto times =
      OneMessageSrcToDest(env.get(), GetParam().body,
                          TimeDelta::FromSeconds(GetParam().allowed_time) +
                              TimeDelta::FromHours(1));

  auto taken_time = env->timer()->Now() - times.connected;
  auto taken_seconds = (taken_time.as_us() + 999999) / 1000000;
  EXPECT_EQ(taken_seconds, GetParam().allowed_time)
      << "sed -i 's/" << GetParam().make_env->name() << "/"
      << EscapeChars("&",
                     ChangeArg(GetParam().make_env->name(), 2, taken_seconds))
      << "/g' " << __FILE__;

  env->DumpAllStats();

  EXPECT_EQ(env->IncomingPacketsAtDestination(), GetParam().expected_packets)
      << "sed -i 's/" << GetParam().make_env->name() << "/"
      << EscapeChars("&", ChangeArg(GetParam().make_env->name(), 3,
                                    env->IncomingPacketsAtDestination()))
      << "/g' " << __FILE__;
}

struct RequestResponseArgs {
  MakeEnv make_env;
  Slice request_body;
  Slice response_body;
  uint32_t allowed_time;
};

std::ostream& operator<<(std::ostream& out, RequestResponseArgs args) {
  return out << args.make_env->name();
}

class RouterEndpoint_RequestResponseIntegration
    : public ::testing::TestWithParam<RequestResponseArgs> {};

TEST_P(RouterEndpoint_RequestResponseIntegration, Works) {
  std::cout << "Param: " << GetParam() << std::endl;
  auto env = GetParam().make_env->Make(Logging());

  auto times = RequestResponse(env.get(), GetParam().request_body,
                               GetParam().response_body,
                               TimeDelta::FromSeconds(GetParam().allowed_time) +
                                   TimeDelta::FromHours(1));

  auto taken_time = env->timer()->Now() - times.connected;
  auto taken_seconds = (taken_time.as_us() + 999999) / 1000000;
  EXPECT_EQ(taken_seconds, GetParam().allowed_time)
      << "sed -i 's/" << GetParam().make_env->name() << "/"
      << EscapeChars("&",
                     ChangeArg(GetParam().make_env->name(), 3, taken_seconds))
      << "/g' " << __FILE__;

  env->DumpAllStats();
}

template <class T, class... Arg>
MakeEnv MakeMakeEnv(const char* name, Arg&&... args) {
  class Impl final : public MakeEnvInterface {
   public:
    Impl(const char* name, std::tuple<Arg...> args)
        : name_(name), args_(args) {}
    const char* name() const { return name_.c_str(); }
    std::shared_ptr<Env> Make(Optional<Severity> logging) const {
      return std::apply(
          [logging](Arg... args) {
            return std::make_shared<T>(logging, args...);
          },
          args_);
    }

   private:
    const std::string name_;
    const std::tuple<Arg...> args_;
  };
  return MakeEnv(
      new Impl(name, std::tuple<Arg...>(std::forward<Arg>(args)...)));
}

// Simulators get abbreviations here because otherwise the environment names get
// too difficult to communicate.
#define DECL_SIM(name, inst) \
  const NamedSimulator name{#name, std::unique_ptr<Simulator>(inst)};

DECL_SIM(Happy, new PacketLinkSimulator(std::make_unique<HappyDelivery>(1500)));
DECL_SIM(Win_3_3, new PacketLinkSimulator(std::make_unique<WindowedDelivery>(
                      3, TimeDelta::FromMilliseconds(3), 256)));
DECL_SIM(ReliableStream, new StreamLinkSimulator<ReliableFramer>(
                             Bandwidth::FromKilobitsPerSecond(1000)));
DECL_SIM(UnreliableStream, new StreamLinkSimulator<UnreliableFramer>(
                               Bandwidth::FromKilobitsPerSecond(115)));

#define MAKE_MAKE_ENV(env, ...) \
  MakeMakeEnv<env>("MAKE_MAKE_ENV(" #env ", " #__VA_ARGS__ ")", __VA_ARGS__)

INSTANTIATE_TEST_SUITE_P(
    RouterEndpoint_IntegrationEnv_Instance, RouterEndpoint_IntegrationEnv,
    ::testing::Values(
        MAKE_MAKE_ENV(TwoNode, &Happy, 1, 2),
        MAKE_MAKE_ENV(TwoNode, &Happy, 2, 1),
        MAKE_MAKE_ENV(TwoNode, &Win_3_3, 1, 2),
        MAKE_MAKE_ENV(TwoNode, &Win_3_3, 2, 1),
        MAKE_MAKE_ENV(TwoNode, &ReliableStream, 1, 2),
        MAKE_MAKE_ENV(TwoNode, &ReliableStream, 2, 1),
        MAKE_MAKE_ENV(TwoNode, &UnreliableStream, 1, 2),
        MAKE_MAKE_ENV(TwoNode, &UnreliableStream, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Happy, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Happy, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Happy, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Happy, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Happy, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Happy, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Win_3_3, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Win_3_3, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Win_3_3, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Win_3_3, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &Win_3_3, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &ReliableStream, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &ReliableStream, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &ReliableStream, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &ReliableStream, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &ReliableStream, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &UnreliableStream, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &UnreliableStream, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &UnreliableStream, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &UnreliableStream, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &Happy, &UnreliableStream, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Happy, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Happy, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Happy, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Happy, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Happy, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Win_3_3, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Win_3_3, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Win_3_3, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Win_3_3, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &Win_3_3, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &ReliableStream, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &ReliableStream, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &ReliableStream, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &ReliableStream, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &ReliableStream, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &UnreliableStream, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &UnreliableStream, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &UnreliableStream, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &UnreliableStream, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &Win_3_3, &UnreliableStream, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Happy, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Happy, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Happy, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Happy, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Happy, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Win_3_3, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Win_3_3, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Win_3_3, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Win_3_3, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Win_3_3, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &Win_3_3, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &ReliableStream, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &ReliableStream, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &ReliableStream, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &ReliableStream, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &ReliableStream, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &UnreliableStream, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &UnreliableStream, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &UnreliableStream, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &UnreliableStream, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &ReliableStream, &UnreliableStream, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Happy, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Happy, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Happy, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Happy, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Happy, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Win_3_3, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Win_3_3, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Win_3_3, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Win_3_3, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &Win_3_3, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &ReliableStream, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &ReliableStream, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &ReliableStream, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &ReliableStream, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &ReliableStream, 3, 2, 1),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &UnreliableStream, 1, 3, 2),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &UnreliableStream, 2, 1, 3),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &UnreliableStream, 2, 3, 1),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &UnreliableStream, 3, 1, 2),
        MAKE_MAKE_ENV(ThreeNode, &UnreliableStream, &UnreliableStream, 3, 2,
                      1)));

#define ONE_MESSAGE_TEST(length, allowed_time, expected_packets, env, ...)   \
  OneMessageArgs {                                                           \
    MakeMakeEnv<env>("ONE_MESSAGE_TEST(" #length ", " #allowed_time          \
                     ", " #expected_packets ", " #env ", " #__VA_ARGS__ ")", \
                     __VA_ARGS__),                                           \
        Slice::RepeatedChar(length, 'a'), allowed_time, expected_packets     \
  }

// clang-format off
INSTANTIATE_TEST_SUITE_P(
    RouterEndpoint_OneMessageIntegration_Instance,
    RouterEndpoint_OneMessageIntegration,
    ::testing::Values(
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &Happy, 1, 2),
        ONE_MESSAGE_TEST(32, 1, 3, TwoNode, &Happy, 1, 2),
        ONE_MESSAGE_TEST(1024, 1, 3, TwoNode, &Happy, 1, 2),
        ONE_MESSAGE_TEST(32768, 1, 25, TwoNode, &Happy, 1, 2),
        ONE_MESSAGE_TEST(1048576, 1, 723, TwoNode, &Happy, 1, 2),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &Happy, 2, 1),
        ONE_MESSAGE_TEST(1048576, 1, 723, TwoNode, &Happy, 2, 1),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &Win_3_3, 1, 2),
        ONE_MESSAGE_TEST(32, 1, 3, TwoNode, &Win_3_3, 1, 2),
        ONE_MESSAGE_TEST(1024, 1, 8, TwoNode, &Win_3_3, 1, 2),
        ONE_MESSAGE_TEST(32768, 1, 166, TwoNode, &Win_3_3, 1, 2),
        ONE_MESSAGE_TEST(1048576, 7, 4998, TwoNode, &Win_3_3, 1, 2),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &Win_3_3, 2, 1),
        ONE_MESSAGE_TEST(1048576, 6, 4995, TwoNode, &Win_3_3, 2, 1),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &ReliableStream, 1, 2),
        ONE_MESSAGE_TEST(32, 1, 3, TwoNode, &ReliableStream, 1, 2),
        ONE_MESSAGE_TEST(1024, 1, 3, TwoNode, &ReliableStream, 1, 2),
        ONE_MESSAGE_TEST(32768, 1, 26, TwoNode, &ReliableStream, 1, 2),
        ONE_MESSAGE_TEST(1048576, 9, 529, TwoNode, &ReliableStream, 1, 2),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &ReliableStream, 2, 1),
        ONE_MESSAGE_TEST(1048576, 9, 529, TwoNode, &ReliableStream, 2, 1),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &UnreliableStream, 1, 2),
        ONE_MESSAGE_TEST(32, 1, 3, TwoNode, &UnreliableStream, 1, 2),
        ONE_MESSAGE_TEST(1024, 1, 7, TwoNode, &UnreliableStream, 1, 2),
        ONE_MESSAGE_TEST(32768, 3, 167, TwoNode, &UnreliableStream, 1, 2),
        ONE_MESSAGE_TEST(1048576, 80, 4873, TwoNode, &UnreliableStream, 1, 2),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &UnreliableStream, 2, 1),
        ONE_MESSAGE_TEST(1048576, 80, 4873, TwoNode, &UnreliableStream, 2, 1),
        ONE_MESSAGE_TEST(1, 1, 26, ThreeNode, &Happy, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 26, ThreeNode, &Happy, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 25, ThreeNode, &Happy, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 65, ThreeNode, &Happy, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 1, 793, ThreeNode, &Happy, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 27, ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 27, ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 33, ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 224, ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 10, 7263, ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 19, ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 19, ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 19, ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 45, ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 9, 743, ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 22, ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 22, ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 29, ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 180, ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 86, 4886, ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 26, ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 26, ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 37, ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 202, ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 7, 5463, ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 27, ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 27, ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 34, ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 216, ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 9, 6332, ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 20, ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 20, ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 27, ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 180, ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 10, 4909, ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 22, ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 22, ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 30, ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 181, ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 86, 4898, ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 25, ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 25, ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 27, ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 70, ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 9, 942, ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 19, ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 19, ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 21, ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 40, ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 9, 539, ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 23, ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 23, ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 28, ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 181, ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 86, 4886, ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 29, ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 30, ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 38, ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 224, ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 86, 5438, ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 30, ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 30, ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 39, ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 248, ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 86, 6181, ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 27, ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 27, ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 31, ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 181, ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 86, 4887, ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 29, ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 29, ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 35, ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 185, ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 86, 4890, ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3)));
// clang-format on

#define REQUEST_RESPONSE_TEST(request_length, response_length, allowed_time, \
                              env, ...)                                      \
  RequestResponseArgs {                                                      \
    MakeMakeEnv<env>("REQUEST_RESPONSE_TEST(" #request_length                \
                     ", " #response_length ", " #allowed_time ", " #env      \
                     ", " #__VA_ARGS__ ")",                                  \
                     __VA_ARGS__),                                           \
        Slice::RepeatedChar(request_length, 'a'),                            \
        Slice::RepeatedChar(response_length, 'b'), allowed_time              \
  }

// clang-format off
INSTANTIATE_TEST_SUITE_P(
    RouterEndpoint_RequestResponse_Instance, RouterEndpoint_RequestResponseIntegration,
    ::testing::Values(
        REQUEST_RESPONSE_TEST(4096, 4096, 1, TwoNode, &Happy, 1, 2),
        REQUEST_RESPONSE_TEST(4096, 4096, 1, TwoNode, &Win_3_3, 1, 2),
        REQUEST_RESPONSE_TEST(4096, 4096, 1, TwoNode, &ReliableStream, 1, 2),
        REQUEST_RESPONSE_TEST(4096, 4096, 1, TwoNode, &UnreliableStream, 1, 2)));
// clang-format on

}  // namespace endpoint_integration_tests
}  // namespace overnet
