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
  bool* value = context.get<TestTransport1>();
  ASSERT_EQ(&input, value);
}

TEST(IncomingContext, CreateAndReceiveDifferentType) {
  bool input = true;
  fidl::internal::IncomingTransportContext context =
      fidl::internal::IncomingTransportContext::Create<TestTransport1>(&input);
  ASSERT_DEATH([&context]() { context.get<TestTransport2>(); });
}

TEST(OutgoingContext, CreateAndReceiveSameType) {
  int64_t input = 123ll;
  fidl::internal::OutgoingTransportContext context =
      fidl::internal::OutgoingTransportContext::Create<TestTransport1>(&input);
  int64_t* value = context.get<TestTransport1>();
  ASSERT_EQ(&input, value);
}

TEST(OutgoingContext, CreateAndReceiveDifferentType) {
  int64_t input = 123ll;
  fidl::internal::OutgoingTransportContext context =
      fidl::internal::OutgoingTransportContext::Create<TestTransport1>(&input);
  ASSERT_DEATH([&context]() { context.get<TestTransport2>(); });
}
