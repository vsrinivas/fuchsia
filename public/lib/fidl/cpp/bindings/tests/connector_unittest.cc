// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <string>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/internal/connector.h"
#include "lib/fidl/cpp/bindings/internal/message_builder.h"
#include "lib/fidl/cpp/bindings/tests/message_queue.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/utility/run_loop.h"

namespace fidl {
namespace test {
namespace {

class ConnectorTest : public testing::Test {
 public:
  ConnectorTest() {}

  void SetUp() override { CreateMessagePipe(nullptr, &handle0_, &handle1_); }

  void TearDown() override {}

  void AllocMessage(const char* text, Message* message) {
    size_t payload_size = strlen(text) + 1;  // Plus null terminator.
    MessageBuilder builder(1, payload_size);
    memcpy(builder.buffer()->Allocate(payload_size), text, payload_size);

    builder.message()->MoveTo(message);
  }

  void PumpMessages() { loop_.RunUntilIdle(); }

 protected:
  mx::msgpipe handle0_;
  mx::msgpipe handle1_;

 private:
  RunLoop loop_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ConnectorTest);
};

class MessageAccumulator : public MessageReceiver {
 public:
  MessageAccumulator() {}

  bool Accept(Message* message) override {
    queue_.Push(message);
    return true;
  }

  bool IsEmpty() const { return queue_.IsEmpty(); }

  void Pop(Message* message) { queue_.Pop(message); }

 private:
  MessageQueue queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MessageAccumulator);
};

TEST_F(ConnectorTest, Basic) {
  internal::Connector connector0(handle0_.Pass());
  internal::Connector connector1(handle1_.Pass());

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

TEST_F(ConnectorTest, Basic_Synchronous) {
  internal::Connector connector0(handle0_.Pass());
  internal::Connector connector1(handle1_.Pass());

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  connector1.WaitForIncomingMessage(MX_TIME_INFINITE);

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

TEST_F(ConnectorTest, Basic_EarlyIncomingReceiver) {
  internal::Connector connector0(handle0_.Pass());
  internal::Connector connector1(handle1_.Pass());

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);

  PumpMessages();

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

TEST_F(ConnectorTest, Basic_TwoMessages) {
  internal::Connector connector0(handle0_.Pass());
  internal::Connector connector1(handle1_.Pass());

  const char* kText[] = {"hello", "world"};

  for (size_t i = 0; i < MOJO_ARRAYSIZE(kText); ++i) {
    Message message;
    AllocMessage(kText[i], &message);

    connector0.Accept(&message);
  }

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  for (size_t i = 0; i < MOJO_ARRAYSIZE(kText); ++i) {
    ASSERT_FALSE(accumulator.IsEmpty());

    Message message_received;
    accumulator.Pop(&message_received);

    EXPECT_EQ(
        std::string(kText[i]),
        std::string(reinterpret_cast<const char*>(message_received.payload())));
  }
}

TEST_F(ConnectorTest, Basic_TwoMessages_Synchronous) {
  internal::Connector connector0(handle0_.Pass());
  internal::Connector connector1(handle1_.Pass());

  const char* kText[] = {"hello", "world"};

  for (size_t i = 0; i < MOJO_ARRAYSIZE(kText); ++i) {
    Message message;
    AllocMessage(kText[i], &message);

    connector0.Accept(&message);
  }

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  connector1.WaitForIncomingMessage(MX_TIME_INFINITE);

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText[0]),
      std::string(reinterpret_cast<const char*>(message_received.payload())));

  ASSERT_TRUE(accumulator.IsEmpty());
}

TEST_F(ConnectorTest, WriteToClosedPipe) {
  internal::Connector connector0(handle0_.Pass());

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  // Close the other end of the pipe.
  handle1_.reset();

  // Not observed yet because we haven't spun the RunLoop yet.
  EXPECT_FALSE(connector0.encountered_error());

  // Write failures are not reported.
  bool ok = connector0.Accept(&message);
  EXPECT_TRUE(ok);

  // Still not observed.
  EXPECT_FALSE(connector0.encountered_error());

  // Spin the RunLoop, and then we should start observing the closed pipe.
  PumpMessages();

  EXPECT_TRUE(connector0.encountered_error());
}

TEST_F(ConnectorTest, MessageWithHandles) {
  internal::Connector connector0(handle0_.Pass());
  internal::Connector connector1(handle1_.Pass());

  const char kText[] = "hello world";

  Message message1;
  AllocMessage(kText, &message1);

  MessagePipe pipe;
  message1.mutable_handles()->push_back(pipe.handle0.release());

  connector0.Accept(&message1);

  // The message should have been transferred, releasing the handles.
  EXPECT_TRUE(message1.handles()->empty());

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
  ASSERT_EQ(1U, message_received.handles()->size());

  // Now send a message to the transferred handle and confirm it's sent through
  // to the orginal pipe.
  mx::msgpipe smph;
  smph.reset(message_received.handles()->front().value());
  message_received.mutable_handles()->front() = Handle();
  // |smph| now owns this handle.

  internal::Connector connector_received(smph.Pass());
  internal::Connector connector_original(pipe.handle1.Pass());

  Message message2;
  AllocMessage(kText, &message2);

  connector_received.Accept(&message2);
  connector_original.set_incoming_receiver(&accumulator);
  PumpMessages();

  ASSERT_FALSE(accumulator.IsEmpty());

  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

TEST_F(ConnectorTest, WaitForIncomingMessageWithError) {
  internal::Connector connector0(handle0_.Pass());
  // Close the other end of the pipe.
  handle1_.reset();
  ASSERT_FALSE(connector0.WaitForIncomingMessage(MX_TIME_INFINITE));
}

class ConnectorDeletingMessageAccumulator : public MessageAccumulator {
 public:
  explicit ConnectorDeletingMessageAccumulator(internal::Connector** connector)
      : connector_(connector) {}

  bool Accept(Message* message) override {
    delete *connector_;
    *connector_ = 0;
    return MessageAccumulator::Accept(message);
  }

 private:
  internal::Connector** connector_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ConnectorDeletingMessageAccumulator);
};

TEST_F(ConnectorTest, WaitForIncomingMessageWithDeletion) {
  internal::Connector connector0(handle0_.Pass());
  internal::Connector* connector1 = new internal::Connector(handle1_.Pass());

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);

  ConnectorDeletingMessageAccumulator accumulator(&connector1);
  connector1->set_incoming_receiver(&accumulator);

  connector1->WaitForIncomingMessage(MX_TIME_INFINITE);

  ASSERT_FALSE(connector1);
  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

class ReentrantMessageAccumulator : public MessageAccumulator {
 public:
  explicit ReentrantMessageAccumulator(internal::Connector* connector)
      : connector_(connector), number_of_calls_(0) {}

  bool Accept(Message* message) override {
    if (!MessageAccumulator::Accept(message))
      return false;
    number_of_calls_++;
    if (number_of_calls_ == 1) {
      return connector_->WaitForIncomingMessage(MX_TIME_INFINITE);
    }
    return true;
  }

  int number_of_calls() { return number_of_calls_; }

 private:
  internal::Connector* connector_;
  int number_of_calls_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ReentrantMessageAccumulator);
};

TEST_F(ConnectorTest, WaitForIncomingMessageWithReentrancy) {
  internal::Connector connector0(handle0_.Pass());
  internal::Connector connector1(handle1_.Pass());

  const char* kText[] = {"hello", "world"};

  for (size_t i = 0; i < MOJO_ARRAYSIZE(kText); ++i) {
    Message message;
    AllocMessage(kText[i], &message);

    connector0.Accept(&message);
  }

  ReentrantMessageAccumulator accumulator(&connector1);
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  for (size_t i = 0; i < MOJO_ARRAYSIZE(kText); ++i) {
    ASSERT_FALSE(accumulator.IsEmpty());

    Message message_received;
    accumulator.Pop(&message_received);

    EXPECT_EQ(
        std::string(kText[i]),
        std::string(reinterpret_cast<const char*>(message_received.payload())));
  }

  ASSERT_EQ(2, accumulator.number_of_calls());
}

// This message receiver just accepts messages, and responds (to another fixed
// receiver)
class NoTaskStarvationReplier : public MessageReceiver {
 public:
  explicit NoTaskStarvationReplier(MessageReceiver* reply_to)
      : reply_to_(reply_to) {
    FTL_CHECK(reply_to_ != this);
  }

  bool Accept(Message* message) override {
    num_accepted_++;

    uint32_t name = message->name();

    if (name >= 10u) {
      RunLoop::current()->PostDelayedTask([]() { RunLoop::current()->Quit(); },
                                          0);
    }

    // We don't necessarily expect the quit task to be processed immediately,
    // but if some large number (say, ten thousand-ish) messages have been
    // processed, we can say that starvation has occurred.
    static const uint32_t kStarvationThreshold = 10000;
    EXPECT_LE(name, kStarvationThreshold);
    // We'd prefer our test not hang, so don't send the reply in the failing
    // case.
    if (name > kStarvationThreshold)
      return true;

    MessageBuilder builder(name + 1u, 0u);
    FTL_CHECK(reply_to_->Accept(builder.message()));

    return true;
  }

  unsigned num_accepted() const { return num_accepted_; }

 private:
  MessageReceiver* const reply_to_;
  unsigned num_accepted_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(NoTaskStarvationReplier);
};

// TODO(vtl): This test currently fails. See the discussion on issue #604
// (https://github.com/domokit/mojo/issues/604).
TEST_F(ConnectorTest, DISABLED_NoTaskStarvation) {
  internal::Connector connector0(handle0_.Pass());
  internal::Connector connector1(handle1_.Pass());

  // The replier will bounce messages to |connector0|, and will receiver
  // messages from |connector1|.
  NoTaskStarvationReplier replier(&connector0);
  connector1.set_incoming_receiver(&replier);

  // Kick things off by sending a messagge on |connector0| (starting with a
  // "name" of 1).
  MessageBuilder builder(1u, 0u);
  ASSERT_TRUE(connector0.Accept(builder.message()));

  PumpMessages();

  EXPECT_GE(replier.num_accepted(), 10u);
}

}  // namespace
}  // namespace test
}  // namespace fidl
