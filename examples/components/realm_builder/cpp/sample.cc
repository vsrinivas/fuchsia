// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START import_statement_cpp]
#include <lib/sys/component/cpp/testing/realm_builder.h>
// [END import_statement_cpp]

#include <fidl/examples/routing/echo/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/string.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

// [START use_namespace_cpp]
// NOLINTNEXTLINE
using namespace component_testing;
// [END use_namespace_cpp]

// [START mock_component_impl_cpp]
class LocalEchoServerImpl : public fidl::examples::routing::echo::Echo, public LocalComponent {
 public:
  explicit LocalEchoServerImpl(async::Loop& loop) : loop_(loop) {}

  // Override `EchoString` from `Echo` protocol.
  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override {
    callback(std::move(value));
    loop_.Quit();
  }

  // Override `Start` from `LocalComponent` class.
  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    // Keep reference to `mock_handles` in member variable.
    // This class contains handles to the component's incoming
    // and outgoing capabilities.
    handles_ = std::move(handles);

    ASSERT_EQ(
        handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, loop_.dispatcher())),
        ZX_OK);
  }

 private:
  async::Loop& loop_;
  fidl::BindingSet<fidl::examples::routing::echo::Echo> bindings_;
  std::unique_ptr<LocalComponentHandles> handles_;
};
// [END mock_component_impl_cpp]

TEST(SampleTest, CallsEcho) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // [START init_realm_builder_cpp]
  auto builder = RealmBuilder::Create();
  // [END init_realm_builder_cpp]

  // [START add_component_cpp]
  // Add component `a` to the realm, which is fetched using a URL.
  builder.AddChild("a", "fuchsia-pkg://fuchsia.com/realm-builder-examples#meta/echo_client.cm");
  // Add component `b` to the realm, which is fetched using a relative URL.
  builder.AddChild("b", "#meta/echo_client.cm");
  // [END add_component_cpp]

  // [START add_legacy_component_cpp]
  // Add component `c` to the realm, which is fetched using a legacy URL.
  builder.AddLegacyChild("c",
                         "fuchsia-pkg://fuchsia.com/realm-builder-examples#meta/echo_client.cmx");
  // [END add_legacy_component_cpp]

  // [START add_mock_component_cpp]
  auto mock_echo_server = LocalEchoServerImpl(loop);

  builder.AddLocalChild("d", &mock_echo_server);
  // [END add_mock_component_cpp]

  // [START route_between_children_cpp]
  builder.AddRoute(Route{.capabilities = {Protocol{"fidl.examples.routing.echo.Echo"}},
                         .source = ChildRef{"d"},
                         .targets = {ChildRef{"a"}, ChildRef{"b"}, ChildRef{"c"}}});
  // [END route_between_children_cpp]

  // [START route_to_test_cpp]
  builder.AddRoute(Route{.capabilities = {Protocol{"fidl.examples.routing.echo.Echo"}},
                         .source = ChildRef{"d"},
                         .targets = {ParentRef()}});
  // [END route_to_test_cpp]

  // [START route_from_test_cpp]
  builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.logger.LogSink"}},
                         .source = ParentRef(),
                         .targets = {ChildRef{"a"}, ChildRef{"b"}, ChildRef{"c"}, ChildRef{"d"}}});
  // [END route_from_test_cpp]

  // [START route_from_test_sibling_cpp]
  builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.example.Foo"}},
                         .source = ParentRef(),
                         .targets = {ChildRef{"a"}}});
  // [END route_from_test_sibling_cpp]

  // [START build_realm_cpp]
  auto realm = builder.Build(loop.dispatcher());
  // [END build_realm_cpp]

  // [START get_child_name_cpp]
  std::cout << "Child Name: {}" << realm.GetChildName() << std::endl;
  // [END get_child_name_cpp]

  // [START call_echo_cpp]
  auto echo = realm.Connect<fidl::examples::routing::echo::Echo>();
  fidl::StringPtr response;
  echo->EchoString("hello", [&](fidl::StringPtr response) {
    ASSERT_EQ(response, "hello");

    loop.Quit();
  });

  loop.Run();
  // [END call_echo_cpp]
}
