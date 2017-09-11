// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <string>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/internal/connector.h"
#include "lib/fidl/cpp/bindings/internal/message_builder.h"
#include "lib/fidl/cpp/bindings/tests/util/message_queue.h"
#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace fidl {
namespace test {
namespace {

class ConnectorTest : public testing::Test {
 public:
  ConnectorTest() {}

  void SetUp() override { mx::channel::create(0, &handle0_, &handle1_); }

  void TearDown() override { ClearAsyncWaiter(); }

  void AllocMessage(const char* text, Message* message) {
    size_t payload_size = strlen(text) + 1;  // Plus null terminator.
    MessageBuilder builder(1, payload_size);
    memcpy(builder.buffer()->Allocate(payload_size), text, payload_size);

    builder.message()->MoveTo(message);
  }

  void PumpMessages() { WaitForAsyncWaiter(); }

 protected:
  mx::channel handle0_;
  mx::channel handle1_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ConnectorTest);
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

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageAccumulator);
};

TEST_F(ConnectorTest, Basic) {
  internal::Connector connector0(std::move(handle0_));
  internal::Connector connector1(std::move(handle1_));

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
  internal::Connector connector0(std::move(handle0_));
  internal::Connector connector1(std::move(handle1_));

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  connector1.WaitForIncomingMessage(fxl::TimeDelta::Max());

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

TEST_F(ConnectorTest, Basic_EarlyIncomingReceiver) {
  internal::Connector connector0(std::move(handle0_));
  internal::Connector connector1(std::move(handle1_));

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
  internal::Connector connector0(std::move(handle0_));
  internal::Connector connector1(std::move(handle1_));

  const char* kText[] = {"hello", "world"};

  for (size_t i = 0; i < arraysize(kText); ++i) {
    Message message;
    AllocMessage(kText[i], &message);

    connector0.Accept(&message);
  }

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  for (size_t i = 0; i < arraysize(kText); ++i) {
    ASSERT_FALSE(accumulator.IsEmpty());

    Message message_received;
    accumulator.Pop(&message_received);

    EXPECT_EQ(
        std::string(kText[i]),
        std::string(reinterpret_cast<const char*>(message_received.payload())));
  }
}

TEST_F(ConnectorTest, Basic_TwoMessages_Synchronous) {
  internal::Connector connector0(std::move(handle0_));
  internal::Connector connector1(std::move(handle1_));

  const char* kText[] = {"hello", "world"};

  for (size_t i = 0; i < arraysize(kText); ++i) {
    Message message;
    AllocMessage(kText[i], &message);

    connector0.Accept(&message);
  }

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  connector1.WaitForIncomingMessage(fxl::TimeDelta::Max());

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText[0]),
      std::string(reinterpret_cast<const char*>(message_received.payload())));

  ASSERT_TRUE(accumulator.IsEmpty());
}

TEST_F(ConnectorTest, MessageWithHandles) {
  internal::Connector connector0(std::move(handle0_));
  internal::Connector connector1(std::move(handle1_));

  const char kText[] = "hello world";

  Message message1;
  AllocMessage(kText, &message1);

  mx::channel handle0, handle1;
  mx::channel::create(0, &handle0, &handle1);
  message1.mutable_handles()->push_back(handle0.release());

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
  mx::channel smph;
  smph.reset(message_received.handles()->front());
  message_received.mutable_handles()->front() = MX_HANDLE_INVALID;
  // |smph| now owns this handle.

  internal::Connector connector_received(std::move(smph));
  internal::Connector connector_original(std::move(handle1));

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
  internal::Connector connector0(std::move(handle0_));
  // Close the other end of the pipe.
  handle1_.reset();
  ASSERT_FALSE(connector0.WaitForIncomingMessage(fxl::TimeDelta::Max()));
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

  FXL_DISALLOW_COPY_AND_ASSIGN(ConnectorDeletingMessageAccumulator);
};

TEST_F(ConnectorTest, WaitForIncomingMessageWithDeletion) {
  internal::Connector connector0(std::move(handle0_));
  internal::Connector* connector1 = new internal::Connector(std::move(handle1_));

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);

  ConnectorDeletingMessageAccumulator accumulator(&connector1);
  connector1->set_incoming_receiver(&accumulator);

  connector1->WaitForIncomingMessage(fxl::TimeDelta::Max());

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
      return connector_->WaitForIncomingMessage(fxl::TimeDelta::Max());
    }
    return true;
  }

  int number_of_calls() { return number_of_calls_; }

 private:
  internal::Connector* connector_;
  int number_of_calls_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ReentrantMessageAccumulator);
};

TEST_F(ConnectorTest, WaitForIncomingMessageWithReentrancy) {
  internal::Connector connector0(std::move(handle0_));
  internal::Connector connector1(std::move(handle1_));

  const char* kText[] = {"hello", "world"};

  for (size_t i = 0; i < arraysize(kText); ++i) {
    Message message;
    AllocMessage(kText[i], &message);

    connector0.Accept(&message);
  }

  ReentrantMessageAccumulator accumulator(&connector1);
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  for (size_t i = 0; i < arraysize(kText); ++i) {
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
    FXL_CHECK(reply_to_ != this);
  }

  bool Accept(Message* message) override {
    num_accepted_++;

    uint32_t name = message->name();

    if (name >= 10u) {
      return true;
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
    FXL_CHECK(reply_to_->Accept(builder.message()));

    return true;
  }

  unsigned num_accepted() const { return num_accepted_; }

 private:
  MessageReceiver* const reply_to_;
  unsigned num_accepted_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(NoTaskStarvationReplier);
};

// TODO(vtl): This test currently fails. See the discussion on issue #604
// (https://github.com/domokit/mojo/issues/604).
TEST_F(ConnectorTest, DISABLED_NoTaskStarvation) {
  internal::Connector connector0(std::move(handle0_));
  internal::Connector connector1(std::move(handle1_));

  // The replier will bounce messages to |connector0|, and will receiver
  // messages from |connector1|.
  NoTaskStarvationReplier replier(&connector0);
  connector1.set_incoming_receiver(&replier);

  // Kick things off by sending a messagge on |connector0| (starting with a
  // "name" of 1).
  MessageBuilder builder(1u, 0u);
  ASSERT_TRUE(connector0.Accept(builder.message()));

  PumpMessages();

  EXPECT_GE(replier.num_accepted(), 9u);
}

}  // namespace
}  // namespace test
}  // namespace fidl
