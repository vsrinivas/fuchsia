// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fit/function.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

#include "lib/sys/cpp/service_directory.h"

namespace {

using namespace sys::testing::experimental;
using namespace sys::testing;

constexpr char kEchoServiceServer[] = "echo_service_server";
constexpr char kEchoServiceServerUrl[] = "#meta/echo_service_server.cm";

class OutgoingDirectoryTest : public gtest::RealLoopFixture {};

TEST_F(OutgoingDirectoryTest, Connects) {
  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddChild(kEchoServiceServer, kEchoServiceServerUrl);
  realm_builder.AddRoute(Route{.capabilities = {Service{fuchsia::examples::EchoService::Name}},
                               .source = ChildRef{kEchoServiceServer},
                               .targets = {ParentRef()}});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                               .source = ParentRef(),
                               .targets = {ChildRef{kEchoServiceServer}}});
  auto realm = realm_builder.Build(dispatcher());

  auto default_service = sys::OpenServiceAt<fuchsia::examples::EchoService>(realm.CloneRoot());
  auto regular = default_service.regular_echo().Connect().Bind();

  constexpr char kMessage[] = "Ping!";
  bool message_replied = false;
  regular->EchoString(kMessage, [expected_reply = kMessage, &message_replied,
                                 quit_loop = QuitLoopClosure()](fidl::StringPtr value) {
    EXPECT_EQ(value, expected_reply);
    message_replied = true;
    quit_loop();
  });

  RunLoop();
  EXPECT_TRUE(message_replied);
}

}  // namespace
