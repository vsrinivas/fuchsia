// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <tuple>

#include "gtest/gtest.h"
#include "src/connectivity/overnet/lib/endpoint/router_endpoint.h"
#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/links/packet_link.h"
#include "src/connectivity/overnet/lib/links/stream_link.h"
#include "src/connectivity/overnet/lib/protocol/fidl.h"
#include "src/connectivity/overnet/lib/protocol/reliable_framer.h"
#include "src/connectivity/overnet/lib/protocol/unreliable_framer.h"
#include "src/connectivity/overnet/lib/testing/flags.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"

//////////////////////////////////////////////////////////////////////////////
// Two node fling

namespace overnet {
namespace router_endpoint2node {

struct LinkState {
  uint64_t id;
  int outstanding_packets;
};

class Simulator {
 public:
  virtual ~Simulator() = default;
  virtual void MakeLinks(RouterEndpoint* a, RouterEndpoint* b, uint64_t id1,
                         uint64_t id2) const = 0;
};

struct NamedSimulator {
  std::string name;
  std::unique_ptr<Simulator> simulator;
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

template <class Impl>
class InProcessLink final : public Link {
 public:
  template <class... Arg>
  InProcessLink(Arg&&... args) : impl_(new Impl(std::forward<Arg>(args)...)) {}

  std::shared_ptr<Impl> get() { return impl_; }

  void Close(Callback<void> quiesced) override {
    impl_->Close(Callback<void>(
        ALLOCATED_CALLBACK,
        [this, quiesced = std::move(quiesced)]() mutable { impl_.reset(); }));
  }
  void Forward(Message message) override { impl_->Forward(std::move(message)); }
  fuchsia::overnet::protocol::LinkStatus GetLinkStatus() override {
    return impl_->GetLinkStatus();
  }
  const LinkStats* GetStats() const override { return impl_->GetStats(); }

 private:
  std::shared_ptr<Impl> impl_;
};

class StatsDumper final : public StatsVisitor {
 public:
  StatsDumper(const char* indent) : indent_(indent) {}
  void Counter(const char* name, uint64_t value) override {
    OVERNET_TRACE(INFO) << indent_ << name << " = " << value;
  }

 private:
  const char* const indent_;
};

template <class T>
void DumpStats(const char* indent, const T* stats) {
  StatsDumper dumper(indent);
  stats->Accept(&dumper);
}

void DumpStats(const char* label, RouterEndpoint* endpoint) {
  OVERNET_TRACE(INFO) << "STATS DUMP FOR: '" << label << "' -- "
                      << endpoint->node_id();
  endpoint->ForEachLink([endpoint](NodeId target, const Link* link) {
    OVERNET_TRACE(INFO) << "  LINK: " << endpoint->node_id() << "->" << target;
    DumpStats("    ", link->GetStats());
  });
}

LinkStats AccumulateLinkStats(RouterEndpoint* endpoint) {
  LinkStats out;
  endpoint->ForEachLink([&out](NodeId target, const Link* link) {
    out.Merge(*link->GetStats());
  });
  return out;
}

class Env {
 public:
  virtual ~Env() {}

  virtual RouterEndpoint* endpoint1() = 0;
  virtual RouterEndpoint* endpoint2() = 0;

  virtual void DumpAllStats() = 0;

  uint64_t OutgoingPacketsFromSource() {
    return AccumulateLinkStats(endpoint1()).outgoing_packet_count;
  }

  uint64_t IncomingPacketsAtDestination() {
    return AccumulateLinkStats(endpoint2()).incoming_packet_count;
  }

  void AwaitConnected() {
    OVERNET_TRACE(INFO) << "Test waiting for connection between "
                        << endpoint1()->node_id() << " and "
                        << endpoint2()->node_id();
    while (!endpoint1()->HasRouteTo(endpoint2()->node_id()) ||
           !endpoint2()->HasRouteTo(endpoint1()->node_id())) {
      endpoint1()->BlockUntilNoBackgroundUpdatesProcessing();
      endpoint2()->BlockUntilNoBackgroundUpdatesProcessing();
      test_timer_.StepUntilNextEvent();
    }
    OVERNET_TRACE(INFO) << "Test connected";
  }

  void FlushTodo(std::function<bool()> until,
                 TimeDelta timeout = TimeDelta::FromMinutes(10)) {
    FlushTodo(until, test_timer_.Now() + timeout);
  }

  void FlushTodo(std::function<bool()> until, TimeStamp deadline) {
    bool stepped = false;
    while (test_timer_.Now() < deadline) {
      if (until())
        break;
      if (!test_timer_.StepUntilNextEvent())
        break;
      stepped = true;
    }
    if (!stepped) {
      test_timer_.Step(TimeDelta::FromMilliseconds(1).as_us());
    }
    ASSERT_LT(test_timer_.Now(), deadline);
  }

  Timer* timer() { return &test_timer_; }

 private:
  TestTimer test_timer_;
  TraceCout trace_cout_{&test_timer_};
  ScopedRenderer scoped_renderer{&trace_cout_};
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};
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

class TwoNode final : public Env {
 public:
  TwoNode(const NamedSimulator* simulator, uint64_t node_id_1,
          uint64_t node_id_2)
      : simulator_(simulator->simulator.get()) {
    endpoint1_ = new RouterEndpoint(timer(), NodeId(node_id_1), false);
    endpoint2_ = new RouterEndpoint(timer(), NodeId(node_id_2), false);
    simulator_->MakeLinks(endpoint1_, endpoint2_, 1, 2);
  }

  void DumpAllStats() override {
    DumpStats("1", endpoint1_);
    DumpStats("2", endpoint2_);
  }

  virtual ~TwoNode() {
    if (!testing::Test::HasFailure()) {
      bool done = false;
      endpoint1_->Close(Callback<void>(ALLOCATED_CALLBACK, [&done, this]() {
        endpoint2_->Close(Callback<void>(ALLOCATED_CALLBACK, [&done, this]() {
          delete endpoint1_;
          delete endpoint2_;
          done = true;
        }));
      }));
      FlushTodo([&done] { return done; });
      EXPECT_TRUE(done);
    }
  }

  RouterEndpoint* endpoint1() override { return endpoint1_; }
  RouterEndpoint* endpoint2() override { return endpoint2_; }

 private:
  const Simulator* const simulator_;
  RouterEndpoint* endpoint1_;
  RouterEndpoint* endpoint2_;
};

class ThreeNode final : public Env {
 public:
  ThreeNode(const NamedSimulator* simulator_1_h,
            const NamedSimulator* simulator_h_2, uint64_t node_id_1,
            uint64_t node_id_h, uint64_t node_id_2)
      : simulator_1_h_(simulator_1_h->simulator.get()),
        simulator_h_2_(simulator_h_2->simulator.get()) {
    endpoint1_ = new RouterEndpoint(timer(), NodeId(node_id_1), false);
    endpointH_ = new RouterEndpoint(timer(), NodeId(node_id_h), false);
    endpoint2_ = new RouterEndpoint(timer(), NodeId(node_id_2), false);
    simulator_1_h_->MakeLinks(endpoint1_, endpointH_, 1, 2);
    simulator_h_2_->MakeLinks(endpointH_, endpoint2_, 3, 4);
  }

  void DumpAllStats() override {
    DumpStats("1", endpoint1_);
    DumpStats("H", endpointH_);
    DumpStats("2", endpoint2_);
  }

  virtual ~ThreeNode() {
    if (!testing::Test::HasFailure()) {
      bool done = false;
      endpointH_->Close(Callback<void>(ALLOCATED_CALLBACK, [this, &done]() {
        endpoint1_->Close(Callback<void>(ALLOCATED_CALLBACK, [this, &done]() {
          endpoint2_->Close(Callback<void>(ALLOCATED_CALLBACK, [this, &done]() {
            delete endpoint1_;
            delete endpoint2_;
            delete endpointH_;
            done = true;
          }));
        }));
      }));
      FlushTodo([&done] { return done; });
      EXPECT_TRUE(done);
    }
  }

  RouterEndpoint* endpoint1() override { return endpoint1_; }
  RouterEndpoint* endpoint2() override { return endpoint2_; }

 private:
  const Simulator* const simulator_1_h_;
  const Simulator* const simulator_h_2_;
  RouterEndpoint* endpoint1_;
  RouterEndpoint* endpointH_;
  RouterEndpoint* endpoint2_;
};

class MakeEnvInterface {
 public:
  virtual const char* name() const = 0;
  virtual std::shared_ptr<Env> Make() const = 0;
};

using MakeEnv = std::shared_ptr<MakeEnvInterface>;

class RouterEndpoint_IntegrationEnv : public ::testing::TestWithParam<MakeEnv> {
};

std::ostream& operator<<(std::ostream& out, MakeEnv env) {
  return out << env->name();
}

class TestService final : public RouterEndpoint::Service {
 public:
  TestService(RouterEndpoint* endpoint, std::string fully_qualified_name,
              fuchsia::overnet::protocol::ReliabilityAndOrdering
                  reliability_and_ordering,
              std::function<void(RouterEndpoint::NewStream)> accept_stream)
      : Service(endpoint, fully_qualified_name, reliability_and_ordering),
        accept_stream_(accept_stream) {}
  void AcceptStream(RouterEndpoint::NewStream stream) override {
    accept_stream_(std::move(stream));
  }

 private:
  std::function<void(RouterEndpoint::NewStream)> accept_stream_;
};

TEST_P(RouterEndpoint_IntegrationEnv, NoOp) {
  std::cout << "Param: " << GetParam() << std::endl;
  GetParam()->Make()->AwaitConnected();
}

TEST_P(RouterEndpoint_IntegrationEnv, NodeDescriptionPropagation) {
  std::cout << "Param: " << GetParam() << std::endl;
  auto env = GetParam()->Make();
  TestService service(
      env->endpoint1(), "#ff00ff",
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      [](auto) { abort(); });
  env->AwaitConnected();
  auto start_wait = env->timer()->Now();
  auto idle_time_done = [&] {
    return env->timer()->Now() - start_wait >= TimeDelta::FromSeconds(5);
  };
  while (!idle_time_done()) {
    env->FlushTodo(idle_time_done);
  }
  bool found = false;
  env->endpoint2()->ForEachNodeDescription(
      [env = env.get(), &found](
          NodeId id, const fuchsia::overnet::protocol::PeerDescription& m) {
        fuchsia::overnet::protocol::PeerDescription want_desc;
        want_desc.mutable_services()->push_back("#ff00ff");
        if (id == env->endpoint1()->node_id()) {
          found = true;
          EXPECT_TRUE(fidl::Equals(m, want_desc));
        }
      });
  EXPECT_TRUE(found);
}

struct OneMessageArgs {
  MakeEnv make_env;
  Slice body;
  TimeDelta allowed_time;
  uint64_t expected_packets;
};

std::ostream& operator<<(std::ostream& out, OneMessageArgs args) {
  return out << args.make_env->name();
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

class RouterEndpoint_OneMessageIntegration
    : public ::testing::TestWithParam<OneMessageArgs> {};

TEST_P(RouterEndpoint_OneMessageIntegration, Works) {
  std::cout << "Param: " << GetParam() << std::endl;
  auto env = GetParam().make_env->Make();

  const std::string kService = "abc";

  const TimeDelta kAllowedTime = GetParam().allowed_time;
  std::cout << "Allowed time for body: " << kAllowedTime << std::endl;

  env->AwaitConnected();

  auto got_pull_cb = std::make_shared<bool>(false);

  TestService service(
      env->endpoint2(), kService,
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      [got_pull_cb](RouterEndpoint::NewStream new_stream) {
        ASSERT_FALSE(*got_pull_cb);
        OVERNET_TRACE(INFO) << "ep2: recv_intro";
        auto stream =
            MakeClosedPtr<RouterEndpoint::Stream>(std::move(new_stream));
        OVERNET_TRACE(INFO) << "ep2: start pull_all";
        auto* op = new RouterEndpoint::ReceiveOp(stream.get());
        OVERNET_TRACE(INFO) << "ep2: op=" << op;
        op->PullAll(StatusOrCallback<Optional<std::vector<Slice>>>(
            ALLOCATED_CALLBACK,
            [got_pull_cb, stream{std::move(stream)},
             op](const StatusOr<Optional<std::vector<Slice>>>& status) mutable {
              OVERNET_TRACE(INFO)
                  << "ep2: pull_all status=" << status.AsStatus();
              EXPECT_TRUE(status.is_ok()) << status.AsStatus();
              EXPECT_TRUE(status->has_value());
              auto pull_text =
                  Slice::Join((*status)->begin(), (*status)->end());
              EXPECT_EQ(GetParam().body, pull_text) << pull_text.AsStdString();
              delete op;
              *got_pull_cb = true;
              OVERNET_TRACE(INFO) << "STATS DUMP FOR RECEIVING DATAGRAM STREAM";
              DumpStats("  ", stream->link_stats());
              DumpStats("  ", stream->stream_stats());
            }));
      });

  ScopedOp scoped_op(Op::New(OpType::OUTGOING_REQUEST));
  auto new_stream = env->endpoint1()->InitiateStream(
      env->endpoint2()->node_id(),
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      kService);
  ASSERT_TRUE(new_stream.is_ok()) << new_stream.AsStatus();
  auto stream =
      MakeClosedPtr<RouterEndpoint::Stream>(std::move(*new_stream.get()));
  RouterEndpoint::SendOp(stream.get(), GetParam().body.length())
      .Push(GetParam().body, Callback<void>::Ignored());

  auto start_flush = env->timer()->Now();

  env->FlushTodo([got_pull_cb]() { return *got_pull_cb; },
                 start_flush + kAllowedTime);

  auto taken_time = env->timer()->Now() - start_flush;
  auto taken_seconds = (taken_time.as_us() + 999999) / 1000000;
  auto allowed_seconds = kAllowedTime.as_us() / 1000000;
  EXPECT_EQ(taken_seconds, allowed_seconds)
      << "sed -i 's/" << GetParam().make_env->name() << "/"
      << EscapeChars("&",
                     ChangeArg(GetParam().make_env->name(), 2, taken_seconds))
      << "/g' " << __FILE__;

  ASSERT_TRUE(*got_pull_cb);

  OVERNET_TRACE(INFO) << "STATS DUMP FOR SENDING DATAGRAM STREAM";
  DumpStats("  ", stream->link_stats());
  DumpStats("  ", stream->stream_stats());

  env->DumpAllStats();

  EXPECT_EQ(env->IncomingPacketsAtDestination(), GetParam().expected_packets)
      << "sed -i 's/" << GetParam().make_env->name() << "/"
      << EscapeChars("&", ChangeArg(GetParam().make_env->name(), 3,
                                    env->IncomingPacketsAtDestination()))
      << "/g' " << __FILE__;
}

template <class T, class... Arg>
MakeEnv MakeMakeEnv(const char* name, Arg&&... args) {
  class Impl final : public MakeEnvInterface {
   public:
    Impl(const char* name, std::tuple<Arg...> args)
        : name_(name), args_(args) {}
    const char* name() const { return name_.c_str(); }
    std::shared_ptr<Env> Make() const {
      return std::apply(
          [](Arg... args) { return std::make_shared<T>(args...); }, args_);
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
        Slice::RepeatedChar(length, 'a'),                                    \
        TimeDelta::FromSeconds(allowed_time), expected_packets               \
  }

// clang-format off
INSTANTIATE_TEST_SUITE_P(
    RouterEndpoint_OneMessageIntegration_Instance,
    RouterEndpoint_OneMessageIntegration,
    ::testing::Values(
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &Happy, 1, 2),
        ONE_MESSAGE_TEST(32, 1, 3, TwoNode, &Happy, 1, 2),
        ONE_MESSAGE_TEST(1024, 1, 3, TwoNode, &Happy, 1, 2),
        ONE_MESSAGE_TEST(32768, 1, 26, TwoNode, &Happy, 1, 2),
        ONE_MESSAGE_TEST(1048576, 1, 720, TwoNode, &Happy, 1, 2),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &Happy, 2, 1),
        ONE_MESSAGE_TEST(1048576, 1, 720, TwoNode, &Happy, 2, 1),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &Win_3_3, 1, 2),
        ONE_MESSAGE_TEST(32, 1, 3, TwoNode, &Win_3_3, 1, 2),
        ONE_MESSAGE_TEST(1024, 1, 8, TwoNode, &Win_3_3, 1, 2),
        ONE_MESSAGE_TEST(32768, 1, 155, TwoNode, &Win_3_3, 1, 2),
        ONE_MESSAGE_TEST(1048576, 7, 4837, TwoNode, &Win_3_3, 1, 2),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &Win_3_3, 2, 1),
        ONE_MESSAGE_TEST(1048576, 7, 4837, TwoNode, &Win_3_3, 2, 1),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &ReliableStream, 1, 2),
        ONE_MESSAGE_TEST(32, 1, 3, TwoNode, &ReliableStream, 1, 2),
        ONE_MESSAGE_TEST(1024, 1, 3, TwoNode, &ReliableStream, 1, 2),
        ONE_MESSAGE_TEST(32768, 1, 26, TwoNode, &ReliableStream, 1, 2),
        ONE_MESSAGE_TEST(1048576, 9, 534, TwoNode, &ReliableStream, 1, 2),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &ReliableStream, 2, 1),
        ONE_MESSAGE_TEST(1048576, 9, 534, TwoNode, &ReliableStream, 2, 1),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &UnreliableStream, 1, 2),
        ONE_MESSAGE_TEST(32, 1, 3, TwoNode, &UnreliableStream, 1, 2),
        ONE_MESSAGE_TEST(1024, 1, 7, TwoNode, &UnreliableStream, 1, 2),
        ONE_MESSAGE_TEST(32768, 3, 165, TwoNode, &UnreliableStream, 1, 2),
        ONE_MESSAGE_TEST(1048576, 79, 4813, TwoNode, &UnreliableStream, 1, 2),
        ONE_MESSAGE_TEST(1, 1, 3, TwoNode, &UnreliableStream, 2, 1),
        ONE_MESSAGE_TEST(1048576, 79, 4813, TwoNode, &UnreliableStream, 2, 1),
        ONE_MESSAGE_TEST(1, 1, 22, ThreeNode, &Happy, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 22, ThreeNode, &Happy, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 22, ThreeNode, &Happy, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 71, ThreeNode, &Happy, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 2, 791, ThreeNode, &Happy, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 24, ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 24, ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 30, ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 189, ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 6, 4977, ThreeNode, &Happy, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 18, ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 18, ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 18, ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 43, ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 9, 748, ThreeNode, &Happy, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 22, ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 22, ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 28, ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 179, ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 84, 4861, ThreeNode, &Happy, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 24, ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 24, ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 28, ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 139, ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 6, 5082, ThreeNode, &Win_3_3, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 26, ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 26, ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 30, ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 183, ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 6, 4880, ThreeNode, &Win_3_3, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 18, ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 18, ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 23, ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 170, ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 10, 4805, ThreeNode, &Win_3_3, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 22, ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 22, ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 27, ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 178, ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 84, 4859, ThreeNode, &Win_3_3, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 24, ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 24, ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 25, ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 67, ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 9, 1361, ThreeNode, &ReliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 18, ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 18, ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 18, ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 1, 37, ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 9, 545, ThreeNode, &ReliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 25, ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 25, ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 29, ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 179, ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 84, 4862, ThreeNode, &ReliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 25, ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 26, ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 38, ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 251, ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 84, 6454, ThreeNode, &UnreliableStream, &Happy, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 35, ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 35, ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 44, ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 256, ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 84, 6473, ThreeNode, &UnreliableStream, &Win_3_3, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 23, ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 23, ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 30, ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 182, ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 84, 4886, ThreeNode, &UnreliableStream, &ReliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1, 1, 28, ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32, 1, 28, ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1024, 1, 32, ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(32768, 3, 184, ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3),
        ONE_MESSAGE_TEST(1048576, 84, 4884, ThreeNode, &UnreliableStream, &UnreliableStream, 1, 2, 3)));
// clang-format on

}  // namespace router_endpoint2node
}  // namespace overnet
