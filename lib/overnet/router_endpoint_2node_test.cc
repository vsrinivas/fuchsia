// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "packet_link.h"
#include "router_endpoint.h"
#include "test_timer.h"
#include "trace_cout.h"

//////////////////////////////////////////////////////////////////////////////
// Two node fling

namespace overnet {
namespace router_endpoint2node {

static const bool kTraceEverything = false;

class InProcessLinkImpl final
    : public PacketLink,
      public std::enable_shared_from_this<InProcessLinkImpl> {
 public:
  InProcessLinkImpl(RouterEndpoint* src, RouterEndpoint* dest,
                    TraceSink trace_sink, uint64_t link_id)
      : PacketLink(src->router(), trace_sink, dest->node_id(), 256),
        timer_(dest->router()->timer()),
        trace_sink_(trace_sink),
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
    timer_->At(
        timer_->Now() + TimeDelta::FromMilliseconds(3),
        Callback<void>(ALLOCATED_CALLBACK, [partner = partner_, from = from_,
                                            now = timer_->Now(), packet,
                                            trace_sink = trace_sink_]() {
          auto strong_partner = partner.lock();
          OVERNET_TRACE(DEBUG, trace_sink)
              << (strong_partner == nullptr ? "DROP" : "EMIT")
              << " PACKET from " << from << " " << packet << "\n";
          if (strong_partner) {
            strong_partner->Process(now, packet);
          }
        }));
  }

 private:
  Timer* const timer_;
  TraceSink trace_sink_;
  std::weak_ptr<InProcessLinkImpl> partner_;
  const NodeId from_;
};

class InProcessLink final : public Link {
 public:
  InProcessLink(RouterEndpoint* src, RouterEndpoint* dest, TraceSink trace_sink,
                uint64_t link_id)
      : impl_(new InProcessLinkImpl(src, dest, trace_sink, link_id)) {}

  std::shared_ptr<InProcessLinkImpl> get() { return impl_; }

  void Close(Callback<void> quiesced) { impl_->Close(std::move(quiesced)); }
  void Forward(Message message) { impl_->Forward(std::move(message)); }
  LinkMetrics GetLinkMetrics() { return impl_->GetLinkMetrics(); }

 private:
  std::shared_ptr<InProcessLinkImpl> impl_;
};

class TwoNodeFling : public ::testing::Test {
 public:
  TwoNodeFling() {
    auto link1 =
        MakeLink<InProcessLink>(endpoint1_, endpoint2_, trace_sink(), 99599104);
    auto link2 =
        MakeLink<InProcessLink>(endpoint2_, endpoint1_, trace_sink(), 99594576);
    link1->get()->Partner(link2->get());
    endpoint1_->router()->RegisterLink(std::move(link1));
    endpoint2_->router()->RegisterLink(std::move(link2));

    while (!endpoint1_->router()->HasRouteTo(NodeId(2)) ||
           !endpoint2_->router()->HasRouteTo(NodeId(1))) {
      endpoint1_->router()->BlockUntilNoBackgroundUpdatesProcessing();
      endpoint2_->router()->BlockUntilNoBackgroundUpdatesProcessing();
      test_timer_.StepUntilNextEvent();
    }
  }

  virtual ~TwoNodeFling() {
    endpoint1_->Close([this]() {
      endpoint2_->Close([this]() {
        delete endpoint1_;
        delete endpoint2_;
      });
    });
    FlushTodo();
  }

  RouterEndpoint* endpoint1() { return endpoint1_; }
  RouterEndpoint* endpoint2() { return endpoint2_; }

  void FlushTodo(std::function<bool()> until = []() { return false; }) {
    const TimeDelta initial_dt = TimeDelta::FromMilliseconds(1);
    TimeDelta dt = initial_dt;
    while (dt < TimeDelta::FromSeconds(30)) {
      if (until())
        return;
      if (test_timer_.StepUntilNextEvent(dt)) {
        dt = initial_dt;
        continue;
      }
      dt = dt + dt;
    }
  }

  TraceSink trace_sink() const { return trace_sink_; }

 private:
  TestTimer test_timer_;
  TraceSink trace_sink_ =
      kTraceEverything ? TraceCout(&test_timer_) : TraceSink();
  RouterEndpoint* endpoint1_ =
      new RouterEndpoint(&test_timer_, trace_sink_, NodeId(1), true);
  RouterEndpoint* endpoint2_ =
      new RouterEndpoint(&test_timer_, trace_sink_, NodeId(2), true);
  Optional<TimeStamp> end_time_;
};

TEST_F(TwoNodeFling, NoOp) {}

struct OneMessageArgs {
  TimeDelta timeout;
  Slice intro;
  Slice body;
};

std::ostream& operator<<(std::ostream& out, OneMessageArgs args) {
  return out << "intro=" << args.intro << " body=" << args.body;
}

class TwoNodeFling_OneMessage
    : public TwoNodeFling,
      public ::testing::WithParamInterface<OneMessageArgs> {};

TEST_P(TwoNodeFling_OneMessage, Works) {
  bool got_pull_cb = false;

  auto intro_status = this->endpoint1()->SendIntro(
      NodeId(2), ReliabilityAndOrdering::ReliableOrdered, GetParam().intro);
  ASSERT_TRUE(intro_status.is_ok()) << intro_status;
  auto stream = MakeClosedPtr<RouterEndpoint::Stream>(
      std::move(*intro_status.get()), trace_sink());
  auto* op = new RouterEndpoint::SendOp(stream.get(), GetParam().body.length());
  op->Push(GetParam().body);
  op->Close(Status::Ok(), [op]() { delete op; });

  this->endpoint2()->RecvIntro(
      StatusOrCallback<RouterEndpoint::ReceivedIntroduction>(
          ALLOCATED_CALLBACK,
          [this, &got_pull_cb](
              StatusOr<RouterEndpoint::ReceivedIntroduction>&& status) {
            OVERNET_TRACE(INFO, trace_sink())
                << "ep2: recv_intro status=" << status.AsStatus();
            ASSERT_TRUE(status.is_ok()) << status.AsStatus();
            auto intro = std::move(*status);
            EXPECT_EQ(GetParam().intro, intro.introduction)
                << intro.introduction.AsStdString();
            auto stream = MakeClosedPtr<RouterEndpoint::Stream>(
                std::move(intro.new_stream), trace_sink());
            auto* op = new RouterEndpoint::ReceiveOp(stream.get());
            op->PullAll(StatusOrCallback<std::vector<Slice>>(
                ALLOCATED_CALLBACK,
                [this, &got_pull_cb, stream{std::move(stream)},
                 op](const StatusOr<std::vector<Slice>>& status) mutable {
                  OVERNET_TRACE(INFO, trace_sink())
                      << "ep2: pull_all status=" << status.AsStatus();
                  EXPECT_TRUE(status.is_ok()) << status.AsStatus();
                  auto pull_text = Slice::Join(status->begin(), status->end());
                  EXPECT_EQ(GetParam().body, pull_text)
                      << pull_text.AsStdString();
                  delete op;
                  got_pull_cb = true;
                }));
          }));

  FlushTodo([&got_pull_cb]() { return got_pull_cb; });

  EXPECT_TRUE(got_pull_cb);
}

INSTANTIATE_TEST_CASE_P(
    TwoNodeFling_OneMessage_Instance, TwoNodeFling_OneMessage,
    ::testing::Values(OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::FromStaticString("abc"),
                                     Slice::FromStaticString("123")},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::FromStaticString("abc"),
                                     Slice::RepeatedChar(3, 'a')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::FromStaticString("abc"),
                                     Slice::RepeatedChar(128, 'a')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::FromStaticString("abc"),
                                     Slice::RepeatedChar(240, 'a')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::FromStaticString("abc"),
                                     Slice::RepeatedChar(256, 'a')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::FromStaticString("abc"),
                                     Slice::RepeatedChar(512, 'a')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(1024, 'a'),
                                     Slice::RepeatedChar(1024, 'b')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(2 * 1024, 'a'),
                                     Slice::RepeatedChar(2 * 1024, 'b')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(4 * 1024, 'a'),
                                     Slice::RepeatedChar(4 * 1024, 'b')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(8 * 1024, 'a'),
                                     Slice::RepeatedChar(8 * 1024, 'b')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(16 * 1024, 'a'),
                                     Slice::RepeatedChar(16 * 1024, 'b')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(32 * 1024, 'a'),
                                     Slice::RepeatedChar(32 * 1024, 'b')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(64 * 1024, 'a'),
                                     Slice::RepeatedChar(64 * 1024, 'b')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(128 * 1024, 'a'),
                                     Slice::RepeatedChar(128 * 1024, 'b')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(256 * 1024, 'a'),
                                     Slice::RepeatedChar(256 * 1024, 'b')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(512 * 1024, 'a'),
                                     Slice::RepeatedChar(512 * 1024, 'b')},
                      OneMessageArgs{TimeDelta::FromSeconds(60),
                                     Slice::RepeatedChar(1024 * 1024, 'a'),
                                     Slice::RepeatedChar(1024 * 1024, 'b')}));

}  // namespace router_endpoint2node
}  // namespace overnet
