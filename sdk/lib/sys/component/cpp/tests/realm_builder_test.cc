// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>
#include <test/placeholders/cpp/fidl.h>

namespace {

using namespace sys::testing::experimental;
using namespace sys::testing;

constexpr char kEchoServerUrl[] =
    "fuchsia-pkg://fuchsia.com/component_cpp_tests#meta/echo_server.cm";
constexpr char kEchoServerLegacyUrl[] =
    "fuchsia-pkg://fuchsia.com/component_cpp_tests#meta/echo_server.cmx";
constexpr char kEchoServerRelativeUrl[] = "#meta/echo_server.cm";

class RealmBuilderTest : public gtest::RealLoopFixture {};

TEST_F(RealmBuilderTest, RoutesProtocolFromChild) {
  static constexpr char kEchoServer[] = "echo_server";

  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddChild(kEchoServer, kEchoServerUrl);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

TEST_F(RealmBuilderTest, RoutesProtocolFromLegacyChild) {
  static constexpr char kEchoServer[] = "echo_server";

  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddLegacyChild(kEchoServer, kEchoServerLegacyUrl);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

TEST_F(RealmBuilderTest, RoutesProtocolFromRelativeChild) {
  static constexpr char kEchoServer[] = "echo_server";

  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddChild(kEchoServer, kEchoServerRelativeUrl);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

class LocalEchoServer : public test::placeholders::Echo, public LocalComponent {
 public:
  explicit LocalEchoServer(fit::closure quit_loop, async_dispatcher_t* dispatcher)
      : quit_loop_(std::move(quit_loop)), dispatcher_(dispatcher), called_(false) {}

  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override {
    callback(std::move(value));
    called_ = true;
    quit_loop_();
  }

  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    handles_ = std::move(handles);
    ASSERT_EQ(handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_)),
              ZX_OK);
  }

  bool WasCalled() const { return called_; }

 private:
  fit::closure quit_loop_;
  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<test::placeholders::Echo> bindings_;
  bool called_;
  std::unique_ptr<LocalComponentHandles> handles_;
};

TEST_F(RealmBuilderTest, RoutesProtocolFromLocalComponent) {
  static constexpr char kEchoServer[] = "echo_server";
  LocalEchoServer local_echo_server(QuitLoopClosure(), dispatcher());
  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddLocalChild(kEchoServer, &local_echo_server);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  test::placeholders::EchoPtr echo;
  ASSERT_EQ(realm.Connect(echo.NewRequest()), ZX_OK);
  echo->EchoString("hello", [](fidl::StringPtr _) {});

  RunLoop();
  EXPECT_TRUE(local_echo_server.WasCalled());
}

class LocalEchoClient : public LocalComponent {
 public:
  explicit LocalEchoClient(fit::closure quit_loop)
      : quit_loop_(std::move(quit_loop)), called_(false) {}

  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    handles_ = std::move(handles);
    test::placeholders::EchoSyncPtr echo;
    ASSERT_EQ(handles_->svc().Connect<test::placeholders::Echo>(echo.NewRequest()), ZX_OK);
    fidl::StringPtr response;
    ASSERT_EQ(echo->EchoString("milk", &response), ZX_OK);
    ASSERT_EQ(response, fidl::StringPtr("milk"));
    called_ = true;
    quit_loop_();
  }

  bool WasCalled() const { return called_; }

 private:
  fit::closure quit_loop_;
  bool called_;
  std::unique_ptr<LocalComponentHandles> handles_;
};

TEST_F(RealmBuilderTest, RoutesProtocolToLocalComponent) {
  static constexpr char kEchoClient[] = "echo_client";
  static constexpr char kEchoServer[] = "echo_server";

  LocalEchoClient local_echo_client(QuitLoopClosure());
  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddLocalChild(kEchoClient, &local_echo_client,
                              ChildOptions{.startup_mode = StartupMode::EAGER});
  realm_builder.AddChild(kEchoServer, kEchoServerUrl);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ChildRef{kEchoClient}}});
  auto realm = realm_builder.Build(dispatcher());
  RunLoop();
  EXPECT_TRUE(local_echo_client.WasCalled());
}

TEST_F(RealmBuilderTest, ConnectsToChannelDirectly) {
  static constexpr char kEchoServer[] = "echo_server";

  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddChild(kEchoServer, kEchoServerUrl);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());

  zx::channel controller, request;
  ASSERT_EQ(zx::channel::create(0, &controller, &request), ZX_OK);
  fidl::SynchronousInterfacePtr<test::placeholders::Echo> echo;
  echo.Bind(std::move(controller));
  ASSERT_EQ(realm.Connect(test::placeholders::Echo::Name_, std::move(request)), ZX_OK);
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

// This test is nearly identicaly to the RealmBuilderTest.RoutesProtocolFromChild
// test case above. The only difference is that it provides a svc directory
// from the sys::Context singleton object to the Realm::Builder::Create method.
// If the test passes, it must follow that Realm::Builder supplied a Context
// object internally, otherwise the test component wouldn't be able to connect
// to fuchsia.component.Realm protocol.
TEST_F(RealmBuilderTest, UsesProvidedSvcDirectory) {
  auto context = sys::ComponentContext::Create();
  static constexpr char kEchoServer[] = "echo_server";

  auto realm_builder = RealmBuilder::Create(context->svc());
  realm_builder.AddChild(kEchoServer, kEchoServerUrl);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

TEST_F(RealmBuilderTest, UsesRandomChildName) {
  std::string child_name_1;
  {
    auto realm_builder = RealmBuilder::Create();
    auto realm = realm_builder.Build(dispatcher());
    child_name_1 = realm.GetChildName();
  }
  std::string child_name_2;
  {
    auto realm_builder = RealmBuilder::Create();
    auto realm = realm_builder.Build(dispatcher());
    child_name_2 = realm.GetChildName();
  }

  EXPECT_NE(child_name_1, child_name_2);
}

TEST_F(RealmBuilderTest, PanicsWhenBuildCalledMultipleTimes) {
  ASSERT_DEATH(
      {
        auto realm_builder = RealmBuilder::Create();
        realm_builder.Build(dispatcher());
        realm_builder.Build(dispatcher());
      },
      "");
}

TEST(RealmBuilderUnittest, PanicsIfChildNameIsEmpty) {
  ASSERT_DEATH(
      {
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddChild("", kEchoServerUrl);
      },
      "");
  ASSERT_DEATH(
      {
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddLegacyChild("", kEchoServerUrl);
      },
      "");

  class BasicLocalImpl : public LocalComponent {};
  ASSERT_DEATH(
      {
        auto realm_builder = RealmBuilder::Create();
        BasicLocalImpl local_impl;
        realm_builder.AddLocalChild("", &local_impl);
      },
      "");
}

TEST(RealmBuilderUnittest, PanicsIfUrlIsEmpty) {
  ASSERT_DEATH(
      {
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddChild("some_valid_name", "");
      },
      "");
  ASSERT_DEATH(
      {
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddLegacyChild("some_valid_name", "");
      },
      "");
}

TEST(RealmBuilderUnittest, PanicsWhenArgsAreNullptr) {
  ASSERT_DEATH(
      {
        auto realm_builder = RealmBuilder::Create();
        // Should panic because |async_get_default_dispatcher| was not configured
        // to return nullptr.
        realm_builder.Build(nullptr);
      },
      "");

  ASSERT_DEATH(
      {
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddLocalChild("some_valid_name", nullptr);
      },
      "");
}
}  // namespace

// Tests for old API. This API is currently deprecated and users should instead
// use the client library under the `experimental` namespace.

namespace old_api {
namespace {
// NOLINTNEXTLINE
using namespace sys::testing;

constexpr char kEchoServerUrl[] =
    "fuchsia-pkg://fuchsia.com/component_cpp_tests#meta/echo_server.cm";
constexpr char kEchoServerLegacyUrl[] =
    "fuchsia-pkg://fuchsia.com/component_cpp_tests#meta/echo_server.cmx";
constexpr char kEchoServerRelativeUrl[] = "#meta/echo_server.cm";

class LegacyRealmBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  }

  void TearDown() override { loop_->Quit(); }

  async_dispatcher_t* dispatcher() { return loop_->dispatcher(); }

  async::Loop& loop() { return *loop_; }

 private:
  std::unique_ptr<async::Loop> loop_;
};

TEST_F(LegacyRealmBuilderTest, RoutesProtocolFromChild) {
  static constexpr auto kEchoServer = Moniker{"echo_server"};

  auto realm_builder = sys::testing::Realm::Builder::Create();
  realm_builder.AddComponent(kEchoServer, Component{.source = ComponentUrl{kEchoServerUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{test::placeholders::Echo::Name_},
                                         .source = kEchoServer,
                                         .targets = {AboveRoot()}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

TEST_F(LegacyRealmBuilderTest, RoutesProtocolFromGrandchild) {
  static constexpr auto kEchoServer = Moniker{"parent/echo_server"};

  auto realm_builder = sys::testing::Realm::Builder::Create();
  realm_builder.AddComponent(kEchoServer, Component{.source = ComponentUrl{kEchoServerUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{test::placeholders::Echo::Name_},
                                         .source = kEchoServer,
                                         .targets = {AboveRoot()}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

TEST_F(LegacyRealmBuilderTest, RoutesProtocolFromLegacyChild) {
  static constexpr auto kEchoServer = Moniker{"echo_server"};

  auto realm_builder = sys::testing::Realm::Builder::Create();
  realm_builder.AddComponent(kEchoServer,
                             Component{.source = LegacyComponentUrl{kEchoServerLegacyUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{test::placeholders::Echo::Name_},
                                         .source = kEchoServer,
                                         .targets = {AboveRoot()}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

TEST_F(LegacyRealmBuilderTest, RoutesProtocolFromRelativeChild) {
  static constexpr auto kEchoServer = Moniker{"echo_server"};

  auto realm_builder = sys::testing::Realm::Builder::Create();
  realm_builder.AddComponent(kEchoServer,
                             Component{.source = ComponentUrl{kEchoServerRelativeUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{test::placeholders::Echo::Name_},
                                         .source = kEchoServer,
                                         .targets = {AboveRoot()}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

class MockEchoServer : public test::placeholders::Echo, public MockComponent {
 public:
  explicit MockEchoServer(async::Loop& loop) : loop_(loop), called_(false) {}

  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override {
    callback(std::move(value));
    called_ = true;
    loop_.Quit();
  }

  void Start(std::unique_ptr<MockHandles> mock_handles) override {
    mock_handles_ = std::move(mock_handles);
    ASSERT_EQ(
        mock_handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, loop_.dispatcher())),
        ZX_OK);
  }

  bool WasCalled() const { return called_; }

 private:
  async::Loop& loop_;
  fidl::BindingSet<test::placeholders::Echo> bindings_;
  bool called_;
  std::unique_ptr<MockHandles> mock_handles_;
};

TEST_F(LegacyRealmBuilderTest, RoutesProtocolFromMockComponent) {
  static constexpr auto kEchoServer = Moniker{"echo_server"};
  MockEchoServer mock_echo_server(loop());
  auto realm_builder = sys::testing::Realm::Builder::Create();
  realm_builder.AddComponent(kEchoServer, Component{
                                              .source = Mock{&mock_echo_server},
                                              .eager = false,
                                          });
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{test::placeholders::Echo::Name_},
                                         .source = kEchoServer,
                                         .targets = {AboveRoot()}});
  auto realm = realm_builder.Build(dispatcher());
  test::placeholders::EchoPtr echo;
  ASSERT_EQ(realm.Connect(echo.NewRequest()), ZX_OK);
  echo->EchoString("hello", [](fidl::StringPtr _) {});

  loop().Run();
  EXPECT_TRUE(mock_echo_server.WasCalled());
}

class MockEchoClient : public MockComponent {
 public:
  explicit MockEchoClient(async::Loop& loop) : loop_(loop), called_(false) {}

  void Start(std::unique_ptr<MockHandles> mock_handles) override {
    mock_handles_ = std::move(mock_handles);
    auto svc = mock_handles_->svc();
    test::placeholders::EchoSyncPtr echo;
    ASSERT_EQ(svc.Connect<test::placeholders::Echo>(echo.NewRequest()), ZX_OK);
    fidl::StringPtr response;
    ASSERT_EQ(echo->EchoString("milk", &response), ZX_OK);
    ASSERT_EQ(response, fidl::StringPtr("milk"));
    called_ = true;
    loop_.Quit();
  }

  bool WasCalled() const { return called_; }

 private:
  async::Loop& loop_;
  bool called_;
  std::unique_ptr<MockHandles> mock_handles_;
};

TEST_F(LegacyRealmBuilderTest, RoutesProtocolToMockComponent) {
  static constexpr auto kEchoClient = Moniker{"echo_client"};
  static constexpr auto kEchoServer = Moniker{"echo_server"};

  MockEchoClient mock_echo_client(loop());
  auto realm_builder = sys::testing::Realm::Builder::Create();
  realm_builder.AddComponent(kEchoClient, Component{
                                              .source = Mock{&mock_echo_client},
                                              .eager = true,
                                          });
  realm_builder.AddComponent(kEchoServer, Component{.source = ComponentUrl{kEchoServerUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{test::placeholders::Echo::Name_},
                                         .source = kEchoServer,
                                         .targets = {kEchoClient}});
  auto realm = realm_builder.Build(dispatcher());
  auto _binder = realm.ConnectSync<fuchsia::component::Binder>();
  loop().Run();
  EXPECT_TRUE(mock_echo_client.WasCalled());
}

TEST_F(LegacyRealmBuilderTest, ConnectsToChannelDirectly) {
  static constexpr auto kEchoServer = Moniker{"echo_server"};

  auto realm_builder = sys::testing::Realm::Builder::Create();
  realm_builder.AddComponent(kEchoServer, Component{.source = ComponentUrl{kEchoServerUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{test::placeholders::Echo::Name_},
                                         .source = kEchoServer,
                                         .targets = {AboveRoot()}});
  auto realm = realm_builder.Build(dispatcher());

  zx::channel controller, request;
  ASSERT_EQ(zx::channel::create(0, &controller, &request), ZX_OK);
  fidl::SynchronousInterfacePtr<test::placeholders::Echo> echo;
  echo.Bind(std::move(controller));
  ASSERT_EQ(realm.Connect(test::placeholders::Echo::Name_, std::move(request)), ZX_OK);
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

// This test is nearly identicaly to the RealmBuilderTest.RoutesProtocolFromChild
// test case above. The only difference is that it provides a svc directory
// from the sys::Context singleton object to the Realm::Builder::Create method.
// If the test passes, it must follow that Realm::Builder supplied a Context
// object internally, otherwise the test component wouldn't be able to connect
// to fuchsia.component.Realm protocol.
TEST_F(LegacyRealmBuilderTest, UsesProvidedSvcDirectory) {
  auto context = sys::ComponentContext::Create();
  static constexpr auto kEchoServer = Moniker{"echo_server"};

  auto realm_builder = sys::testing::Realm::Builder::Create(context->svc());
  realm_builder.AddComponent(kEchoServer, Component{.source = ComponentUrl{kEchoServerUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{test::placeholders::Echo::Name_},
                                         .source = kEchoServer,
                                         .targets = {AboveRoot()}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

TEST_F(LegacyRealmBuilderTest, UsesRandomChildName) {
  std::string child_name_1 = "";
  {
    auto realm_builder = sys::testing::Realm::Builder::Create();
    auto realm = realm_builder.Build(dispatcher());
    child_name_1 = realm.GetChildName();
  }
  std::string child_name_2 = "";
  {
    auto realm_builder = sys::testing::Realm::Builder::Create();
    auto realm = realm_builder.Build(dispatcher());
    child_name_2 = realm.GetChildName();
  }

  EXPECT_NE(child_name_1, child_name_2);
}

TEST_F(LegacyRealmBuilderTest, PanicsWhenBuildCalledMultipleTimes) {
  ASSERT_DEATH(
      {
        auto realm_builder = sys::testing::Realm::Builder::Create();
        realm_builder.Build(dispatcher());
        realm_builder.Build(dispatcher());
      },
      "");
}

TEST(LegacyRealmBuilderUnittest, PanicsIfMonikerIsBad) {
  auto context = sys::ComponentContext::Create();
  ASSERT_DEATH(
      {
        auto realm_builder = sys::testing::Realm::Builder::Create();
        realm_builder.AddComponent(Moniker{""}, Component{
                                                    .source = ComponentUrl{},
                                                });
      },
      "");
  ASSERT_DEATH(
      {
        auto realm_builder = sys::testing::Realm::Builder::Create();
        realm_builder.AddComponent(Moniker{"/no_leading_slash"}, Component{
                                                                     .source = ComponentUrl{},
                                                                 });
      },
      "");

  ASSERT_DEATH(
      {
        auto realm_builder = sys::testing::Realm::Builder::Create();
        realm_builder.AddComponent(Moniker{"no_trailing_slash/"}, Component{
                                                                      .source = ComponentUrl{},
                                                                  });
      },
      "");
}

TEST(LegacyRealmBuilderUnittest, PanicsWhenArgsAreNullptr) {
  ASSERT_DEATH(
      {
        auto realm_builder = sys::testing::Realm::Builder::Create();
        // Should panic because |async_get_default_dispatcher| was not configured
        // to return nullptr.
        realm_builder.Build(nullptr);
      },
      "");
}

}  // namespace
}  // namespace old_api
