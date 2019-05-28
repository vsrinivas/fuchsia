// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/endpoint/integration_tests/tests.h"

namespace overnet {
namespace endpoint_integration_tests {

namespace {
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
}  // namespace

void NoOpTest(Env* env) { env->AwaitConnected(); }

void NodeDescriptionPropagationTest(Env* env) {
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
      [env, &found](NodeId id,
                    const fuchsia::overnet::protocol::PeerDescription& m) {
        fuchsia::overnet::protocol::PeerDescription want_desc;
        want_desc.mutable_services()->push_back("#ff00ff");
        if (id == env->endpoint1()->node_id()) {
          found = true;
          ZX_ASSERT(fidl::Equals(m, want_desc));
        }
      });
  ZX_ASSERT(found);
}

TestTimes OneMessageSrcToDest(Env* env, Slice body,
                              Optional<TimeDelta> allowed_time) {
  const std::string kService = "abc";

  env->AwaitConnected();

  auto got_pull_cb = std::make_shared<bool>(false);

  TestService service(
      env->endpoint2(), kService,
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      [got_pull_cb, body](RouterEndpoint::NewStream new_stream) {
        ZX_ASSERT(!*got_pull_cb);
        OVERNET_TRACE(INFO) << "ep2: recv_intro";
        auto stream =
            MakeClosedPtr<RouterEndpoint::Stream>(std::move(new_stream));
        OVERNET_TRACE(INFO) << "ep2: start pull_all";
        auto* op = new RouterEndpoint::ReceiveOp(stream.get());
        OVERNET_TRACE(INFO) << "ep2: op=" << op;
        op->PullAll(StatusOrCallback<Optional<std::vector<Slice>>>(
            ALLOCATED_CALLBACK,
            [got_pull_cb, body, stream{std::move(stream)},
             op](const StatusOr<Optional<std::vector<Slice>>>& status) mutable {
              OVERNET_TRACE(INFO)
                  << "ep2: pull_all status=" << status.AsStatus();
              ZX_ASSERT(status.is_ok());
              ZX_ASSERT(status->has_value());
              auto pull_text =
                  Slice::Join((*status)->begin(), (*status)->end());
              ZX_ASSERT(body == pull_text);
              delete op;
              *got_pull_cb = true;
            }));
      });

  ScopedOp scoped_op(Op::New(OpType::OUTGOING_REQUEST));
  auto new_stream = env->endpoint1()->InitiateStream(
      env->endpoint2()->node_id(),
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      kService);
  ZX_ASSERT(new_stream.is_ok());
  auto stream =
      MakeClosedPtr<RouterEndpoint::Stream>(std::move(*new_stream.get()));
  RouterEndpoint::SendOp(stream.get(), body.length())
      .Push(body, Callback<void>::Ignored());

  auto start_flush = env->timer()->Now();

  env->FlushTodo([got_pull_cb]() { return *got_pull_cb; },
                 allowed_time.has_value()
                     ? start_flush + *allowed_time
                     : TimeStamp::AfterEpoch(TimeDelta::PositiveInf()));

  ZX_ASSERT(*got_pull_cb);

  return TestTimes{start_flush};
}

TestTimes RequestResponse(Env* env, Slice request_body, Slice response_body,
                          Optional<TimeDelta> allowed_time) {
  const std::string kService = "abc";

  env->AwaitConnected();

  auto got_response = std::make_shared<bool>(false);

  TestService service(
      env->endpoint2(), kService,
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      [request_body, response_body](RouterEndpoint::NewStream new_stream) {
        OVERNET_TRACE(INFO) << "ep2: recv_intro";
        auto stream =
            MakeClosedPtr<RouterEndpoint::Stream>(std::move(new_stream));
        OVERNET_TRACE(INFO) << "ep2: start pull_all";
        auto* op = new RouterEndpoint::ReceiveOp(stream.get());
        OVERNET_TRACE(INFO) << "ep2: op=" << op;
        op->PullAll(StatusOrCallback<Optional<std::vector<Slice>>>(
            ALLOCATED_CALLBACK,
            [request_body, response_body, stream{std::move(stream)},
             op](const StatusOr<Optional<std::vector<Slice>>>& status) mutable {
              OVERNET_TRACE(INFO)
                  << "ep2: pull_all status=" << status.AsStatus();
              ZX_ASSERT(status.is_ok());
              ZX_ASSERT(status->has_value());
              auto pull_text =
                  Slice::Join((*status)->begin(), (*status)->end());
              ZX_ASSERT(request_body == pull_text);
              delete op;
              RouterEndpoint::SendOp(stream.get(), response_body.length())
                  .Push(response_body, Callback<void>::Ignored());
            }));
      });

  ScopedOp scoped_op(Op::New(OpType::OUTGOING_REQUEST));
  auto new_stream = env->endpoint1()->InitiateStream(
      env->endpoint2()->node_id(),
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      kService);
  ZX_ASSERT(new_stream.is_ok());
  auto stream =
      MakeClosedPtr<RouterEndpoint::Stream>(std::move(*new_stream.get()));
  RouterEndpoint::SendOp(stream.get(), request_body.length())
      .Push(request_body, Callback<void>::Ignored());
  auto* op = new RouterEndpoint::ReceiveOp(stream.get());
  OVERNET_TRACE(INFO) << "ep1: op=" << op;
  op->PullAll(StatusOrCallback<Optional<std::vector<Slice>>>(
      ALLOCATED_CALLBACK,
      [response_body, stream{std::move(stream)}, op, got_response](
          const StatusOr<Optional<std::vector<Slice>>>& status) mutable {
        OVERNET_TRACE(INFO) << "ep1: pull_all status=" << status.AsStatus();
        ZX_ASSERT(status.is_ok());
        ZX_ASSERT(status->has_value());
        auto pull_text = Slice::Join((*status)->begin(), (*status)->end());
        ZX_ASSERT(response_body == pull_text);
        delete op;
        *got_response = true;
      }));

  auto start_flush = env->timer()->Now();

  env->FlushTodo([got_response]() { return *got_response; },
                 allowed_time.has_value()
                     ? start_flush + *allowed_time
                     : TimeStamp::AfterEpoch(TimeDelta::PositiveInf()));

  ZX_ASSERT(*got_response);

  return TestTimes{start_flush};
}

}  // namespace endpoint_integration_tests
}  // namespace overnet
