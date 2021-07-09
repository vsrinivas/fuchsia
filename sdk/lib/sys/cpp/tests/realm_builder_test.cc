// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/string.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/realm_builder.h>

#include <gtest/gtest.h>
#include <test/placeholders/cpp/fidl.h>

// NOLINTNEXTLINE
using namespace sys::testing;

constexpr char kEchoServerUrl[] =
    "fuchsia-pkg://fuchsia.com/component_cpp_tests#meta/echo_server.cm";
constexpr char kEchoServerLegacyUrl[] =
    "fuchsia-pkg://fuchsia.com/component_cpp_tests#meta/echo_server.cmx";

TEST(RealmBuilderTest, RoutesProtocolFromChild) {
  auto context = sys::ComponentContext::Create();
  auto realm_builder = Realm::Builder::New(context.get());
  realm_builder.AddComponent(Moniker{"echo_server"},
                             Component{.source = ComponentUrl{kEchoServerUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"test.placeholders.Echo"},
                                         .source = Moniker{"echo_server"},
                                         .targets = {AboveRoot()}});
  auto realm = realm_builder.Build(context.get());
  test::placeholders::EchoSyncPtr echo_proxy;
  ASSERT_EQ(realm.Connect(echo_proxy.NewRequest()), ZX_OK);
  fidl::StringPtr response;
  ASSERT_EQ(echo_proxy->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

TEST(RealmBuilderTest, RoutesProtocolFromGrandchild) {
  auto context = sys::ComponentContext::Create();
  auto realm_builder = Realm::Builder::New(context.get());
  realm_builder.AddComponent(Moniker{"parent/echo_server"},
                             Component{.source = ComponentUrl{kEchoServerUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"test.placeholders.Echo"},
                                         .source = Moniker{"parent/echo_server"},
                                         .targets = {AboveRoot()}});
  auto realm = realm_builder.Build(context.get());
  test::placeholders::EchoSyncPtr echo_proxy;
  ASSERT_EQ(realm.Connect(echo_proxy.NewRequest()), ZX_OK);
  fidl::StringPtr response;
  ASSERT_EQ(echo_proxy->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

TEST(RealmBuilderTest, RoutesProtocolFromLegacyChild) {
  auto context = sys::ComponentContext::Create();
  auto realm_builder = Realm::Builder::New(context.get());
  realm_builder.AddComponent(Moniker{"echo_server"},
                             Component{.source = LegacyComponentUrl{kEchoServerLegacyUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"test.placeholders.Echo"},
                                         .source = Moniker{"echo_server"},
                                         .targets = {AboveRoot()}});
  auto realm = realm_builder.Build(context.get());
  test::placeholders::EchoSyncPtr echo_proxy;
  ASSERT_EQ(realm.Connect(echo_proxy.NewRequest()), ZX_OK);
  fidl::StringPtr response;
  ASSERT_EQ(echo_proxy->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}
