// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_CLIENT_SUITE_HARNESS_HARNESS_H_
#define SRC_TESTS_FIDL_CLIENT_SUITE_HARNESS_HARNESS_H_

#include <fidl/fidl.clientsuite/cpp/fidl.h>

#include <deque>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"
#include "src/tests/fidl/channel_util/channel.h"

#define WAIT_UNTIL(condition) ASSERT_TRUE(_wait_until(condition));
#define WAIT_UNTIL_CALLBACK_RUN() ASSERT_TRUE(_wait_until([&]() { return WasCallbackRun(); }));

namespace client_suite {

#define CLIENT_TEST(test_name)                                                           \
  struct ClientTestWrapper##test_name : public ClientTest {                              \
    ClientTestWrapper##test_name() : ClientTest(fidl_clientsuite::Test::k##test_name) {} \
  };                                                                                     \
  TEST_F(ClientTestWrapper##test_name, test_name)

template <typename Reporter, typename Event>
class EventReporter : public fidl::Server<Reporter> {
 public:
  EventReporter() = default;
  EventReporter(EventReporter&&) = delete;
  EventReporter(EventReporter&) = delete;
  EventReporter& operator=(EventReporter&&) = delete;
  EventReporter& operator=(EventReporter&) = delete;

  void ReportEvent(
      typename fidl::Server<Reporter>::ReportEventRequest& request,
      typename fidl::Server<Reporter>::ReportEventCompleter::Sync& completer) override {
    received_events_.emplace_back(std::move(request));
  }

  size_t NumReceivedEvents() { return received_events_.size(); }

  Event TakeNextEvent() {
    ZX_ASSERT(NumReceivedEvents());
    Event event = std::move(received_events_.front());
    received_events_.pop_front();
    return event;
  }

 private:
  std::deque<Event> received_events_;
};

using ClosedTargetEventReporter = EventReporter<fidl_clientsuite::ClosedTargetEventReporter,
                                                fidl_clientsuite::ClosedTargetEventReport>;
using AjarTargetEventReporter = EventReporter<fidl_clientsuite::AjarTargetEventReporter,
                                              fidl_clientsuite::AjarTargetEventReport>;
using OpenTargetEventReporter = EventReporter<fidl_clientsuite::OpenTargetEventReporter,
                                              fidl_clientsuite::OpenTargetEventReport>;

class ClientTest : public ::loop_fixture::RealLoop, public ::testing::Test {
 protected:
  static constexpr zx::duration kTimeoutDuration = zx::sec(5);

  explicit ClientTest(fidl_clientsuite::Test test) : test_(test) {}

  void SetUp() override;
  void TearDown() override;

  fidl::Client<fidl_clientsuite::Runner>& runner() { return runner_; }
  channel_util::Channel& server_end() { return server_; }

  // Take the client end of the channel corresponding to |server_end| as a
  // |ClosedTarget| |ClientEnd|.
  fidl::ClientEnd<fidl_clientsuite::ClosedTarget> TakeClosedClient() {
    return fidl::ClientEnd<fidl_clientsuite::ClosedTarget>(std::move(client_));
  }

  // Take the client end of the channel corresponding to |server_end| as an
  // |AjarTarget| |ClientEnd|.
  fidl::ClientEnd<fidl_clientsuite::AjarTarget> TakeAjarClient() {
    return fidl::ClientEnd<fidl_clientsuite::AjarTarget>(std::move(client_));
  }

  // Take the client end of the channel corresponding to |server_end| as an
  // |OpenTarget| |ClientEnd|.
  fidl::ClientEnd<fidl_clientsuite::OpenTarget> TakeOpenClient() {
    return fidl::ClientEnd<fidl_clientsuite::OpenTarget>(std::move(client_));
  }

  // Tell the runner to start receiving events on the closed target. Returns the
  // ClosedTargetEventReporter which can be used to check what events are seen
  // by the client.
  std::shared_ptr<ClosedTargetEventReporter> ReceiveClosedEvents();

  // Tell the runner to start receiving events on the ajar target. Returns the
  // AjarTargetEventReporter which can be used to check what events are seen by
  // the client.
  std::shared_ptr<AjarTargetEventReporter> ReceiveAjarEvents();

  // Tell the runner to start receiving events on the open target. Returns the
  // OpenTargetEventReporter which can be used to check what events are seen by
  // the client.
  std::shared_ptr<OpenTargetEventReporter> ReceiveOpenEvents();

  // Use WAIT_UNTIL instead of calling |_wait_until| directly.
  bool _wait_until(fit::function<bool()> condition) {
    return RunLoopWithTimeoutOrUntil(std::move(condition), kTimeoutDuration);
  }

  // Wait for a NaturalThenable to complete. Call this on an async method to
  // finish waiting for it synchronously.
  template <typename FidlMethod>
  fidl::Result<FidlMethod> WaitFor(fidl::internal::NaturalThenable<FidlMethod>&& thenable) {
    std::optional<fidl::Result<FidlMethod>> result_out;
    std::move(thenable).ThenExactlyOnce(
        [&result_out](auto& result) { result_out = std::move(result); });
    RunLoopUntil([&result_out]() { return result_out.has_value(); });
    return std::move(result_out.value());
  }

  // Mark that the callback was run.
  void MarkCallbackRun() { ran_callback_ = true; }
  // Check whether the callback was run.
  bool WasCallbackRun() const { return ran_callback_; }

 private:
  fidl_clientsuite::Test test_;

  fidl::Client<fidl_clientsuite::Runner> runner_;

  zx::channel client_;
  channel_util::Channel server_;

  std::optional<fidl::ServerBindingRef<fidl_clientsuite::ClosedTargetEventReporter>>
      closed_target_reporter_binding_;
  std::optional<fidl::ServerBindingRef<fidl_clientsuite::AjarTargetEventReporter>>
      ajar_target_reporter_binding_;
  std::optional<fidl::ServerBindingRef<fidl_clientsuite::OpenTargetEventReporter>>
      open_target_reporter_binding_;

  bool ran_callback_ = false;
};

}  // namespace client_suite

#endif  // SRC_TESTS_FIDL_CLIENT_SUITE_HARNESS_HARNESS_H_
