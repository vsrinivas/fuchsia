// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/synchronous_connector.h"

#include <string.h>

#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/internal/message_builder.h"
#include "lib/fidl/cpp/bindings/message.h"

namespace fidl {
namespace test {
namespace {

void AllocMessage(const std::string& text, Message* message) {
  size_t payload_size = text.length() + 1;  // Plus null terminator.
  MessageBuilder builder(1, payload_size);
  memcpy(builder.buffer()->Allocate(payload_size), text.c_str(), payload_size);

  builder.message()->MoveTo(message);
}

// Simple success case.
TEST(SynchronousConnectorTest, Basic) {
  zx::channel handle0, handle1;
  zx::channel::create(0, &handle0, &handle1);
  internal::SynchronousConnector connector0(std::move(handle0));
  internal::SynchronousConnector connector1(std::move(handle1));

  const std::string kText("hello world");
  Message message;
  AllocMessage(kText, &message);

  EXPECT_TRUE(connector0.Write(&message));

  Message message_received;
  EXPECT_TRUE(connector1.BlockingRead(&message_received));

  EXPECT_EQ(
      kText,
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

// Writing to a closed pipe should fail.
TEST(SynchronousConnectorTest, WriteToClosedPipe) {
  zx::channel handle0, handle1;
  zx::channel::create(0, &handle0, &handle1);
  internal::SynchronousConnector connector0(std::move(handle0));

  // Close the other end of the pipe.
  handle1.reset();

  Message message;
  EXPECT_FALSE(connector0.Write(&message));
}

// Reading from a closed pipe should fail (while waiting on it).
TEST(SynchronousConnectorTest, ReadFromClosedPipe) {
  zx::channel handle0, handle1;
  zx::channel::create(0, &handle0, &handle1);
  internal::SynchronousConnector connector0(std::move(handle0));

  // Close the other end of the pipe.
  handle1.reset();

  Message message;
  EXPECT_FALSE(connector0.BlockingRead(&message));
}

}  // namespace
}  // namespace test
}  // namespace fidl
