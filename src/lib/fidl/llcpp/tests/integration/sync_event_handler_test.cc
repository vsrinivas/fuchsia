// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.coding.fuchsia/cpp/wire.h>
#include <fidl/fidl.test.coding.fuchsia/cpp/wire_test_base.h>

#include <type_traits>

#include <zxtest/zxtest.h>

namespace test = fidl_test_coding_fuchsia;

namespace {

TEST(SyncEventHandler, TestBase) {
  zx::status endpoints = fidl::CreateEndpoints<test::TwoEvents>();
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

}  // namespace
