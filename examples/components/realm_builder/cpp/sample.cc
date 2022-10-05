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
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

// [START use_namespace_cpp]
// NOLINTNEXTLINE
using namespace component_testing;
// [END use_namespace_cpp]

class RealmBuilderTest : public ::gtest::RealLoopFixture {};

// This test demonstrates constructing a realm with two child components
// and verifying the `fidl.examples.routing.Echo` protocol.
TEST_F(RealmBuilderTest, RoutesFromEcho) {
  // [START init_realm_builder_cpp]
  auto builder = RealmBuilder::Create();
  // [END init_realm_builder_cpp]

  // [START add_component_cpp]
  // [START add_server_cpp]
  // Add component server to the realm, which is fetched using a URL.
  builder.AddChild("echo_server",
                   "fuchsia-pkg://fuchsia.com/realm-builder-examples#meta/echo_server.cm");
  // [END add_server_cpp]
  // Add component to the realm, which is fetched using a relative URL. The
  // child is not exposing a service, so the `EAGER` option ensures the child
  // starts when the realm is built.
  builder.AddChild("echo_client", "#meta/echo_client.cm",
                   ChildOptions{.startup_mode = StartupMode::EAGER});
  // [END add_component_cpp]

  // [START route_between_children_cpp]
  builder.AddRoute(Route{.capabilities = {Protocol{"fidl.examples.routing.echo.Echo"}},
                         .source = ChildRef{"echo_server"},
                         .targets = {ChildRef{"echo_client"}}});
  // [END route_between_children_cpp]

  // [START route_to_test_cpp]
  builder.AddRoute(Route{.capabilities = {Protocol{"fidl.examples.routing.echo.Echo"}},
                         .source = ChildRef{"echo_server"},
                         .targets = {ParentRef()}});
  // [END route_to_test_cpp]

  // [START route_from_test_cpp]
  builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.logger.LogSink"}},
                         .source = ParentRef(),
                         .targets = {ChildRef{"echo_server"}, ChildRef{"echo_client"}}});
  // [END route_from_test_cpp]

  // [START build_realm_cpp]
  auto realm = builder.Build(dispatcher());
  // [END build_realm_cpp]

  // [START get_child_name_cpp]
  std::cout << "Child Name: " << realm.GetChildName() << std::endl;
  // [END get_child_name_cpp]

  // [START call_echo_cpp]
  auto echo = realm.ConnectSync<fidl::examples::routing::echo::Echo>();
  fidl::StringPtr response;
  echo->EchoString("hello", &response);
  ASSERT_EQ(response, "hello");
  // [END call_echo_cpp]
}

// [START mock_component_impl_cpp]
class LocalEchoServerImpl : public fidl::examples::routing::echo::Echo, public LocalComponent {
 public:
  explicit LocalEchoServerImpl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // Override `EchoString` from `Echo` protocol.
  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override {
    callback(std::move(value));
  }

  // Override `Start` from `LocalComponent` class.
  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    // Keep reference to `handles` in member variable.
    // This class contains handles to the component's incoming
    // and outgoing capabilities.
    handles_ = std::move(handles);

    ASSERT_EQ(handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_)),
              ZX_OK);
  }

 private:
  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<fidl::examples::routing::echo::Echo> bindings_;
  std::unique_ptr<LocalComponentHandles> handles_;
};
// [END mock_component_impl_cpp]

// This test demonstrates constructing a realm with a mocked LocalComponent
// implementation of the `fidl.examples.routing.Echo` protocol.
TEST_F(RealmBuilderTest, RoutesFromMockEcho) {
  auto builder = RealmBuilder::Create();

  // [START add_mock_component_cpp]
  // Add component to the realm, providing a mock implementation
  builder.AddLocalChild("echo_server",
                        [&]() { return std::make_unique<LocalEchoServerImpl>(dispatcher()); });
  // [END add_mock_component_cpp]

  builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.logger.LogSink"}},
                         .source = ParentRef(),
                         .targets = {ChildRef{"echo_server"}}});

  builder.AddRoute(Route{.capabilities = {Protocol{"fidl.examples.routing.echo.Echo"}},
                         .source = ChildRef{"echo_server"},
                         .targets = {ParentRef()}});

  auto realm = builder.Build(dispatcher());

  auto echo = realm.Connect<fidl::examples::routing::echo::Echo>();
  fidl::StringPtr response;
  echo->EchoString("hello", [&](fidl::StringPtr response) {
    // Use EXPECT here so the loop can still quit if the test fails
    EXPECT_EQ(response, "hello");
    QuitLoop();
  });

  // Wait for async callback to complete
  RunLoop();
}
