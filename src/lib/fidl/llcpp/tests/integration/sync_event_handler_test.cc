// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.basic.protocol/cpp/wire.h>
#include <fidl/test.basic.protocol/cpp/wire_test_base.h>
#include <fidl/test.transitional/cpp/wire.h>

#include <type_traits>

#include <zxtest/zxtest.h>

namespace test = test_basic_protocol;

namespace {

TEST(SyncEventHandler, TestBase) {
  zx::result endpoints = fidl::CreateEndpoints<test::TwoEvents>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(fidl::WireSendEvent(endpoints->server)->EventA());

  struct EventHandler : public fidl::testing::WireSyncEventHandlerTestBase<test::TwoEvents> {
    void NotImplemented_(const std::string& name) override {
      EXPECT_EQ(std::string_view{"EventA"}, name);
      called = true;
    }
    bool called = false;
  };
  EventHandler event_handler;
  ASSERT_OK(event_handler.HandleOneEvent(endpoints->client));
  EXPECT_TRUE(event_handler.called);
}

TEST(SyncEventHandler, ExhaustivenessRequired) {
  class EventHandlerNone : public fidl::WireSyncEventHandler<test::TwoEvents> {};
  class EventHandlerA : public fidl::WireSyncEventHandler<test::TwoEvents> {
    void EventA(fidl::WireEvent<test::TwoEvents::EventA>*) override {}
  };
  class EventHandlerB : public fidl::WireSyncEventHandler<test::TwoEvents> {
    void EventB(fidl::WireEvent<test::TwoEvents::EventB>*) override {}
  };
  class EventHandlerAll : public fidl::WireSyncEventHandler<test::TwoEvents> {
    void EventA(fidl::WireEvent<test::TwoEvents::EventA>*) override {}
    void EventB(fidl::WireEvent<test::TwoEvents::EventB>*) override {}
  };
  class EventHandlerAllTransitional
      : public fidl::WireSyncEventHandler<test_transitional::TransitionalEvent> {};
  static_assert(std::is_abstract_v<EventHandlerNone>);
  static_assert(std::is_abstract_v<EventHandlerA>);
  static_assert(std::is_abstract_v<EventHandlerB>);
  static_assert(!std::is_abstract_v<EventHandlerAll>);
  static_assert(!std::is_abstract_v<EventHandlerAllTransitional>);
}

TEST(SyncEventHandler, HandleEvent) {
  zx::result endpoints = fidl::CreateEndpoints<test::TwoEvents>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(fidl::WireSendEvent(endpoints->server)->EventA());

  struct EventHandlerAll : public fidl::WireSyncEventHandler<test::TwoEvents> {
    void EventA(fidl::WireEvent<test::TwoEvents::EventA>*) override { count += 1; }
    void EventB(fidl::WireEvent<test::TwoEvents::EventB>*) override {
      ADD_FAILURE("Should not get EventB");
    }
    int count = 0;
  };

  EventHandlerAll event_handler;
  ASSERT_OK(event_handler.HandleOneEvent(endpoints->client));
  EXPECT_EQ(1, event_handler.count);
}

TEST(SyncEventHandler, UnknownEvent) {
  zx::result endpoints = fidl::CreateEndpoints<test::TwoEvents>();
  ASSERT_OK(endpoints.status_value());
  constexpr uint64_t kUnknownOrdinal = 0x1234abcd1234abcdULL;
  static_assert(kUnknownOrdinal != fidl::internal::WireOrdinal<test::TwoEvents::EventA>::value);
  static_assert(kUnknownOrdinal != fidl::internal::WireOrdinal<test::TwoEvents::EventB>::value);
  fidl_message_header_t unknown_message = {
      .txid = 1,
      .at_rest_flags = {0, 0},
      .dynamic_flags = 0,
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = kUnknownOrdinal,
  };
  ASSERT_OK(
      endpoints->server.channel().write(0, &unknown_message, sizeof(unknown_message), nullptr, 0));

  class EventHandlerAll : public fidl::WireSyncEventHandler<test::TwoEvents> {
    void EventA(fidl::WireEvent<test::TwoEvents::EventA>*) override {
      ADD_FAILURE("Should not get EventA");
    }
    void EventB(fidl::WireEvent<test::TwoEvents::EventB>*) override {
      ADD_FAILURE("Should not get EventB");
    }
  };
  EventHandlerAll event_handler;
  fidl::Status status = event_handler.HandleOneEvent(endpoints->client);
  EXPECT_EQ(fidl::Reason::kUnexpectedMessage, status.reason());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status.status());
  EXPECT_SUBSTR(status.FormatDescription().c_str(), "unknown ordinal");
}

TEST(SyncEventHandler, UnhandledTransitionalEvent) {
  zx::result endpoints = fidl::CreateEndpoints<test_transitional::TransitionalEvent>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(fidl::WireSendEvent(endpoints->server)->Event());

  class EventHandler : public fidl::WireSyncEventHandler<test_transitional::TransitionalEvent> {};
  EventHandler event_handler;
  fidl::Status status = event_handler.HandleOneEvent(endpoints->client);
  EXPECT_EQ(fidl::Reason::kUnexpectedMessage, status.reason());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status.status());
  EXPECT_SUBSTR(status.FormatDescription().c_str(), "transitional");
}

}  // namespace
