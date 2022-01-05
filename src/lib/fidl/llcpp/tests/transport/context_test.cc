// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/transport.h>

#include <zxtest/zxtest.h>

struct TestTransport1 {
  using IncomingTransportContextType = bool;
  using OutgoingTransportContextType = int64_t;
  static constexpr const fidl::internal::TransportVTable VTable = {
      .type = FIDL_TRANSPORT_TYPE_TEST,
  };
};

struct TestTransport2 {
  using IncomingTransportContextType = void;
  using OutgoingTransportContextType = void;
  static constexpr const fidl::internal::TransportVTable VTable = {
      .type = FIDL_TRANSPORT_TYPE_CHANNEL,
  };
};

TEST(IncomingContext, CreateAndReceiveSameType) {
  bool input = true;
  fidl::internal::IncomingTransportContext context =
      fidl::internal::IncomingTransportContext::Create<TestTransport1>(&input);
  fidl::internal::IncomingTransportContext moved_context = std::move(context);
  ASSERT_DEATH([&]() { context.release<TestTransport1>(); });

  bool* value = moved_context.release<TestTransport1>();
  ASSERT_EQ(&input, value);
  ASSERT_DEATH([&]() { moved_context.release<TestTransport1>(); });
}

TEST(IncomingContext, CreateAndReceiveDifferentType) {
  bool input = true;
  fidl::internal::IncomingTransportContext context =
      fidl::internal::IncomingTransportContext::Create<TestTransport1>(&input);
  ASSERT_DEATH([&]() { context.release<TestTransport2>(); });
}

TEST(OutgoingContext, CreateAndReceiveSameType) {
  int64_t input = 123ll;
  fidl::internal::OutgoingTransportContext context =
      fidl::internal::OutgoingTransportContext::Create<TestTransport1>(&input);
  fidl::internal::OutgoingTransportContext moved_context = std::move(context);
  ASSERT_DEATH([&]() { context.release<TestTransport1>(); });

  int64_t* value = moved_context.release<TestTransport1>();
  ASSERT_EQ(&input, value);
  ASSERT_DEATH([&]() { moved_context.release<TestTransport1>(); });
}

TEST(OutgoingContext, CreateAndReceiveDifferentType) {
  int64_t input = 123ll;
  fidl::internal::OutgoingTransportContext context =
      fidl::internal::OutgoingTransportContext::Create<TestTransport1>(&input);
  ASSERT_DEATH([&]() { context.release<TestTransport2>(); });
}

void close_incoming_transport_context(void* value) {
  uint8_t* uintval = static_cast<uint8_t*>(value);
  (*uintval)++;
}

void close_outgoing_transport_context(void* value) {
  uint16_t* uintval = static_cast<uint16_t*>(value);
  (*uintval)--;
}

struct ClosingTestTransport {
  using IncomingTransportContextType = uint8_t;
  using OutgoingTransportContextType = uint16_t;
  static constexpr const fidl::internal::TransportVTable VTable = {
      .type = FIDL_TRANSPORT_TYPE_TEST,
      .close_incoming_transport_context = close_incoming_transport_context,
      .close_outgoing_transport_context = close_outgoing_transport_context,
  };
};

TEST(IncomingContext, Closing) {
  uint8_t input = 1;
  { auto unused = fidl::internal::IncomingTransportContext::Create<ClosingTestTransport>(&input); }
  ASSERT_EQ(2, input);
}

TEST(OutgoingContext, Closing) {
  uint16_t input = 1;
  { auto unused = fidl::internal::OutgoingTransportContext::Create<ClosingTestTransport>(&input); }
  ASSERT_EQ(0, input);
}
