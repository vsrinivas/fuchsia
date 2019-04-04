// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <tuple>
#include "gtest/gtest.h"
#include "src/connectivity/overnet/lib/endpoint/router_endpoint.h"
#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/links/packet_link.h"
#include "src/connectivity/overnet/lib/protocol/fidl.h"
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

class DeliverySimulator {
 public:
  virtual ~DeliverySimulator() {}
  // Returns Nothing if the slice should be dropped, or a delay if it should be
  // delivered.
  virtual Optional<TimeDelta> ChoosePacketDelivery(LinkState link_state,
                                                   size_t slice_size) const = 0;

  virtual std::string name() = 0;

  virtual Bandwidth SimulatedBandwidth() const = 0;
};

// Very fast reliable packet delivery: it's often easier to debug problems
// with HappyDelivery than with packet loss enabled (assuming it shows up).
class HappyDelivery : public DeliverySimulator {
 public:
  Optional<TimeDelta> ChoosePacketDelivery(LinkState link_state,
                                           size_t slice_size) const override {
    return TimeDelta::FromMicroseconds(1);
  }

  std::string name() override { return "HappyDelivery"; }

  Bandwidth SimulatedBandwidth() const override {
    return Bandwidth::FromKilobitsPerSecond(1024 * 1024);
  }
};

// Windowed number of outstanding packets
class WindowedDelivery : public DeliverySimulator {
 public:
  WindowedDelivery(int max_outstanding, TimeDelta window)
      : max_outstanding_(max_outstanding), window_(window) {}

  Optional<TimeDelta> ChoosePacketDelivery(LinkState link_state,
                                           size_t slice_size) const override {
    if (link_state.outstanding_packets >= max_outstanding_)
      return Nothing;
    return window_;
  }

  std::string name() override {
    std::ostringstream out;
    out << "WindowedDelivery{max:" << max_outstanding_ << " over " << window_
        << "}";
    return out.str();
  }

  Bandwidth SimulatedBandwidth() const override {
    return Bandwidth::BytesPerTime(256 * max_outstanding_, window_);
  }

 private:
  const int max_outstanding_;
  const TimeDelta window_;
};

class InProcessLinkImpl final
    : public PacketLink,
      public std::enable_shared_from_this<InProcessLinkImpl> {
 public:
  InProcessLinkImpl(RouterEndpoint* src, RouterEndpoint* dest, uint64_t link_id,
                    const DeliverySimulator* simulator)
      : PacketLink(src, dest->node_id(), 256, link_id),
        timer_(dest->timer()),
        link_id_(link_id),
        simulator_(simulator),
        from_(src->node_id()) {
    src->RegisterPeer(dest->node_id());
  }

  ~InProcessLinkImpl() {
    auto strong_partner = partner_.lock();
    if (strong_partner != nullptr) {
      strong_partner->partner_.reset();
    }
  }

  void Partner(std::shared_ptr<InProcessLinkImpl> other) {
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
  const DeliverySimulator* const simulator_;
  std::weak_ptr<InProcessLinkImpl> partner_;
  const NodeId from_;
  int outstanding_packets_ = 0;
};

class InProcessLink final : public Link {
 public:
  InProcessLink(RouterEndpoint* src, RouterEndpoint* dest, uint64_t link_id,
                const DeliverySimulator* simulator)
      : impl_(new InProcessLinkImpl(src, dest, link_id, simulator)) {}

  std::shared_ptr<InProcessLinkImpl> get() { return impl_; }

  void Close(Callback<void> quiesced) override {
    impl_->Close(Callback<void>(
        ALLOCATED_CALLBACK,
        [this, quiesced = std::move(quiesced)]() mutable { impl_.reset(); }));
  }
  void Forward(Message message) override { impl_->Forward(std::move(message)); }
  fuchsia::overnet::protocol::LinkStatus GetLinkStatus() override {
    return impl_->GetLinkStatus();
  }

 private:
  std::shared_ptr<InProcessLinkImpl> impl_;
};

class Env {
 public:
  virtual ~Env() {}

  virtual TimeDelta AllowedTime(uint64_t length) const = 0;

  virtual RouterEndpoint* endpoint1() = 0;
  virtual RouterEndpoint* endpoint2() = 0;

  void AwaitConnected() {
    OVERNET_TRACE(INFO) << "Test waiting for connection";
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

class TwoNode final : public Env {
 public:
  TwoNode(const DeliverySimulator* simulator, uint64_t node_id_1,
          uint64_t node_id_2)
      : simulator_(simulator) {
    endpoint1_ = new RouterEndpoint(timer(), NodeId(node_id_1), false);
    endpoint2_ = new RouterEndpoint(timer(), NodeId(node_id_2), false);
    auto link1 = MakeLink<InProcessLink>(endpoint1_, endpoint2_, 1, simulator);
    auto link2 = MakeLink<InProcessLink>(endpoint2_, endpoint1_, 2, simulator);
    link1->get()->Partner(link2->get());
    endpoint1_->RegisterLink(std::move(link1));
    endpoint2_->RegisterLink(std::move(link2));
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

  TimeDelta AllowedTime(uint64_t data_length) const override {
    // TODO(ctiller): make this just
    // 'simulator_->SimulatedBandwidth().SendTimeForBytes(data_length)'
    return TimeDelta::FromSeconds(1) +
           simulator_->SimulatedBandwidth().SendTimeForBytes(3 * data_length);
  }

  RouterEndpoint* endpoint1() override { return endpoint1_; }
  RouterEndpoint* endpoint2() override { return endpoint2_; }

 private:
  const DeliverySimulator* const simulator_;
  RouterEndpoint* endpoint1_;
  RouterEndpoint* endpoint2_;
};

class ThreeNode final : public Env {
 public:
  ThreeNode(const DeliverySimulator* simulator, uint64_t node_id_1,
            uint64_t node_id_2, uint64_t node_id_h)
      : simulator_(simulator) {
    endpoint1_ = new RouterEndpoint(timer(), NodeId(node_id_1), false);
    endpointH_ = new RouterEndpoint(timer(), NodeId(node_id_h), false);
    endpoint2_ = new RouterEndpoint(timer(), NodeId(node_id_2), false);
    auto link1H = MakeLink<InProcessLink>(endpoint1_, endpointH_, 1, simulator);
    auto linkH1 = MakeLink<InProcessLink>(endpointH_, endpoint1_, 2, simulator);
    auto link2H = MakeLink<InProcessLink>(endpoint2_, endpointH_, 3, simulator);
    auto linkH2 = MakeLink<InProcessLink>(endpointH_, endpoint2_, 4, simulator);
    link1H->get()->Partner(linkH1->get());
    link2H->get()->Partner(linkH2->get());
    endpoint1_->RegisterLink(std::move(link1H));
    endpoint2_->RegisterLink(std::move(link2H));
    endpointH_->RegisterLink(std::move(linkH1));
    endpointH_->RegisterLink(std::move(linkH2));
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

  TimeDelta AllowedTime(uint64_t data_length) const override {
    // TODO(ctiller): make this just
    // 'simulator_->SimulatedBandwidth().SendTimeForBytes(data_length)'
    return TimeDelta::FromSeconds(4) +
           simulator_->SimulatedBandwidth().SendTimeForBytes(5 * data_length);
  }

  RouterEndpoint* endpoint1() override { return endpoint1_; }
  RouterEndpoint* endpoint2() override { return endpoint2_; }

 private:
  const DeliverySimulator* const simulator_;
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

static const auto kSimulators = [] {
  std::vector<std::unique_ptr<DeliverySimulator>> out;
  out.emplace_back(new HappyDelivery());
  out.emplace_back(new WindowedDelivery(3, TimeDelta::FromMilliseconds(3)));
  return out;
}();

template <class Env, int N>
void AddVariations(const char* base_name, std::vector<MakeEnv>* envs) {
  for (const auto& simulator : kSimulators) {
    std::array<int, N> ary;
    for (int i = 0; i < N; i++) {
      ary[i] = i + 1;
    }
    do {
      std::ostringstream name;
      name << base_name;
      for (auto i : ary)
        name << i;
      name << ":";
      name << simulator->name();
      envs->emplace_back(std::apply(
          [&name, simulator = simulator.get()](auto... args) {
            return MakeMakeEnv<Env>(
                name.str().c_str(),
                std::forward<DeliverySimulator* const>(simulator),
                std::forward<int>(args)...);
          },
          ary));
    } while (std::next_permutation(ary.begin(), ary.end()));
  }
}

const auto kEnvVariations = [] {
  std::vector<MakeEnv> envs;
  AddVariations<TwoNode, 2>("TwoNode", &envs);
  AddVariations<ThreeNode, 3>("ThreeNode", &envs);
  return envs;
}();

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

INSTANTIATE_TEST_SUITE_P(RouterEndpoint_IntegrationEnv_Instance,
                         RouterEndpoint_IntegrationEnv,
                         ::testing::ValuesIn(kEnvVariations.begin(),
                                             kEnvVariations.end()));

struct OneMessageArgs {
  MakeEnv make_env;
  Slice body;
};

std::ostream& operator<<(std::ostream& out, OneMessageArgs args) {
  return out << "env=" << args.make_env->name() << " body=" << args.body;
}

class RouterEndpoint_OneMessageIntegration
    : public ::testing::TestWithParam<OneMessageArgs> {};

TEST_P(RouterEndpoint_OneMessageIntegration, Works) {
  std::cout << "Param: " << GetParam() << std::endl;
  auto env = GetParam().make_env->Make();

  const std::string kService = "abc";

  const TimeDelta kAllowedTime =
      env->AllowedTime(kService.length() + GetParam().body.length());
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

  env->FlushTodo([got_pull_cb]() { return *got_pull_cb; },
                 env->timer()->Now() + kAllowedTime);

  ASSERT_TRUE(*got_pull_cb);
}

const std::vector<OneMessageArgs> kOneMessageArgTests = [] {
  std::vector<OneMessageArgs> out;
  const std::vector<size_t> kLengths = {1, 32, 1024, 32768, 1048576};
  for (MakeEnv make_env : kEnvVariations) {
    for (auto payload_length : kLengths) {
      out.emplace_back(
          OneMessageArgs{make_env, Slice::RepeatedChar(payload_length, 'a')});
    }
  }
  return out;
}();

INSTANTIATE_TEST_SUITE_P(RouterEndpoint_OneMessageIntegration_Instance,
                         RouterEndpoint_OneMessageIntegration,
                         ::testing::ValuesIn(kOneMessageArgTests.begin(),
                                             kOneMessageArgTests.end()));

}  // namespace router_endpoint2node
}  // namespace overnet
