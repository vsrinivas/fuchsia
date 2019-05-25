// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/stream_socket_link.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include "src/connectivity/overnet/lib/embedded/basic_overnet_embedded.h"
#include "src/connectivity/overnet/lib/protocol/fidl.h"
#include "src/connectivity/overnet/lib/protocol/reliable_framer.h"
#include "src/connectivity/overnet/lib/protocol/unreliable_framer.h"
#include "src/connectivity/overnet/lib/testing/flags.h"
#include "src/connectivity/overnet/lib/vocabulary/socket.h"

namespace overnet {
namespace stream_socket_link_test {

template <class F, bool E, bool RT>
struct TestTraits {
  using Framer = F;
  static constexpr bool kEagerAnnounce = E;
  static constexpr TimeDelta kReadTimeout =
      RT ? TimeDelta::FromMilliseconds(100) : TimeDelta::PositiveInf();
};

using ReliableEager = TestTraits<ReliableFramer, true, false>;
using ReliableUneager = TestTraits<ReliableFramer, false, false>;
using UnreliableEager = TestTraits<UnreliableFramer, true, true>;
using UnreliableUneager = TestTraits<UnreliableFramer, false, true>;

template <class Traits>
class TypedTest : public ::testing::Test {
 public:
  void SetUp() override {
    int sv[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
    mocked_socket_ = Socket(sv[1]);
    ASSERT_TRUE(mocked_socket_.SetNonBlocking(true).is_ok());
    RegisterStreamSocketLink(&app_, Socket(sv[0]),
                             std::make_unique<typename Traits::Framer>(),
                             Traits::kEagerAnnounce, Traits::kReadTimeout,
                             [this] { destroyed_ = true; });
  }

  void TearDown() override {
    app_.Exit(Status::Ok());
    app_.Run();
  }

  void Write(Slice stuff) {
    auto write_result = mocked_socket_.Write(std::move(stuff));
    ASSERT_TRUE(write_result.is_ok()) << write_result;
    if (write_result->length()) {
      bool done = false;
      app_.reactor()->OnWrite(
          mocked_socket_.get(),
          StatusCallback(ALLOCATED_CALLBACK,
                         [this, &done, remaining = std::move(*write_result)](
                             const Status& status) mutable {
                           ASSERT_TRUE(status.is_ok()) << status;
                           done = true;
                           Write(std::move(remaining));
                         }));
      StepUntil(&done);
    }
  }

  StatusOr<Optional<Slice>> PollFrame() { return mocked_side_framer_.Pop(); }

  StatusOr<Slice> ReadFrame() {
    auto poll_result = PollFrame();
    if (poll_result.is_error()) {
      return poll_result.AsStatus();
    }
    if (poll_result->has_value()) {
      return std::move(**poll_result);
    }
    bool done = false;
    app_.reactor()->OnRead(
        mocked_socket_.get(),
        StatusCallback(ALLOCATED_CALLBACK, [this, &done](const Status& status) {
          ASSERT_TRUE(status.is_ok()) << status;
          done = true;
          auto input = mocked_socket_.Read(4096);
          ASSERT_TRUE(input.is_ok()) << input;
          if (input->has_value()) {
            mocked_side_framer_.Push(std::move(**input));
          }
        }));
    StepUntil(&done);
    return ReadFrame();
  }

  template <class F>
  StatusOr<F> ReadAndDecode() {
    return ReadFrame().Then(
        [](Slice slice) { return Decode<F>(std::move(slice)); });
  }

  Slice Frame(Slice stuff) { return mocked_side_framer_.Frame(stuff); }

  bool RouterHasRouteTo(NodeId node_id) {
    return app_.endpoint()->HasRouteTo(node_id);
  }

  template <class F>
  bool WaitFor(F succeeds, TimeDelta timeout) {
    return app_.reactor()->WaitUntil(std::move(succeeds),
                                     app_.timer()->Now() + timeout);
  }

  void StepUntil(bool* done) {
    Timeout timeout(app_.timer(),
                    app_.timer()->Now() + TimeDelta::FromSeconds(5),
                    [](const Status& status) {
                      if (status.is_ok()) {
                        abort();
                      }
                    });
    while (!*done) {
      app_.reactor()->Step();
    }
  }

 private:
  BasicOvernetEmbedded app_{false};
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};
  typename Traits::Framer mocked_side_framer_;
  Socket mocked_socket_;
  bool destroyed_ = false;
};

TYPED_TEST_SUITE_P(TypedTest);

static Slice EncodedGreeting() {
  fuchsia::overnet::protocol::StreamSocketGreeting greeting;
  greeting.set_magic_string("Fuchsia Socket Stream");
  greeting.set_node_id(NodeId(0x1111'2222'3333'4444).as_fidl());
  greeting.set_local_link_id(1);
  return *Encode(&greeting);
}

TYPED_TEST_P(TypedTest, Connects) {
  this->Write(this->Frame(EncodedGreeting()));

  auto got_greeting = this->template ReadAndDecode<
      fuchsia::overnet::protocol::StreamSocketGreeting>();
  ASSERT_TRUE(got_greeting.is_ok()) << got_greeting;
  ASSERT_TRUE(got_greeting->has_magic_string());
  EXPECT_EQ(got_greeting->magic_string(), "Fuchsia Socket Stream");
  EXPECT_TRUE(got_greeting->has_node_id());
  EXPECT_TRUE(got_greeting->has_local_link_id());

  EXPECT_TRUE(this->WaitFor(
      [this] { return this->RouterHasRouteTo(NodeId(0x1111'2222'3333'4444)); },
      TimeDelta::FromMilliseconds(200)));
}

REGISTER_TYPED_TEST_SUITE_P(TypedTest, Connects);
using FramerTypes = ::testing::Types<ReliableEager, ReliableUneager,
                                     UnreliableEager, UnreliableUneager>;
INSTANTIATE_TYPED_TEST_SUITE_P(StreamSocketLinkSuite, TypedTest, FramerTypes);

struct UnreliableUneagerTest : public TypedTest<UnreliableUneager>,
                               public ::testing::WithParamInterface<Slice> {};

TEST_P(UnreliableUneagerTest, Connects) {
  OVERNET_TRACE(INFO) << GetParam();
  this->Write(GetParam());

  auto got_greeting = this->template ReadAndDecode<
      fuchsia::overnet::protocol::StreamSocketGreeting>();
  ASSERT_TRUE(got_greeting.is_ok()) << got_greeting;
  ASSERT_TRUE(got_greeting->has_magic_string());
  EXPECT_EQ(got_greeting->magic_string(), "Fuchsia Socket Stream");
  EXPECT_TRUE(got_greeting->has_node_id());
  EXPECT_TRUE(got_greeting->has_local_link_id());

  EXPECT_TRUE(this->WaitFor(
      [this] { return this->RouterHasRouteTo(NodeId(0x1111'2222'3333'4444)); },
      TimeDelta::FromMilliseconds(200)));
}

const Slice kUnreliableStreamGreetingFrame =
    UnreliableFramer().Frame(EncodedGreeting());

INSTANTIATE_TEST_SUITE_P(
    UnreliableUneagerSuite, UnreliableUneagerTest,
    ::testing::Values(
        kUnreliableStreamGreetingFrame,
        Slice::Join(
            {kUnreliableStreamGreetingFrame,
             Slice::FromStaticString("\nsome random trailing bytes\n")}),
        Slice::Join(
            {kUnreliableStreamGreetingFrame,
             Slice::FromStaticString(
                 "some random trailing bytes without the extra newlines")}),
        Slice::Join({Slice::FromStaticString("not a frame"),
                     kUnreliableStreamGreetingFrame}),
        Slice::Join({Slice::FromStaticString("\n\xff"),
                     kUnreliableStreamGreetingFrame})));

}  // namespace stream_socket_link_test
}  // namespace overnet
