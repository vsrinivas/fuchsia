// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/string.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/realm_builder.h>
#include <lib/sys/cpp/testing/realm_builder_types.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>
#include <test/placeholders/cpp/fidl.h>

namespace {

// NOLINTNEXTLINE
using namespace sys::testing;

constexpr char kEchoServerUrl[] =
    "fuchsia-pkg://fuchsia.com/component_cpp_tests#meta/echo_server.cm";
constexpr char kEchoServerLegacyUrl[] =
    "fuchsia-pkg://fuchsia.com/component_cpp_tests#meta/echo_server.cmx";
constexpr char kEchoServerRelativeUrl[] = "#meta/echo_server.cm";

class RealmBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    context_ = sys::ComponentContext::Create();
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  }

  void TearDown() override { loop_->Quit(); }

  async_dispatcher_t* dispatcher() { return loop_->dispatcher(); }

  async::Loop& loop() { return *loop_; }

  sys::ComponentContext* context() { return context_.get(); }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<sys::ComponentContext> context_;
};

TEST_F(RealmBuilderTest, RoutesProtocolFromChild) {
  static constexpr auto kEchoServer = Moniker{"echo_server"};

  auto realm_builder = Realm::Builder::New(context());
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

TEST_F(RealmBuilderTest, RoutesProtocolFromGrandchild) {
  static constexpr auto kEchoServer = Moniker{"parent/echo_server"};

  auto realm_builder = Realm::Builder::New(context());
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

TEST_F(RealmBuilderTest, RoutesProtocolFromLegacyChild) {
  static constexpr auto kEchoServer = Moniker{"echo_server"};

  auto realm_builder = Realm::Builder::New(context());
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

TEST_F(RealmBuilderTest, RoutesProtocolFromRelativeChild) {
  static constexpr auto kEchoServer = Moniker{"echo_server"};

  auto realm_builder = Realm::Builder::New(context());
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

TEST_F(RealmBuilderTest, RoutesProtocolFromMockComponent) {
  static constexpr auto kEchoServer = Moniker{"echo_server"};
  MockEchoServer mock_echo_server(loop());
  auto realm_builder = Realm::Builder::New(context());
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

TEST_F(RealmBuilderTest, RoutesProtocolToMockComponent) {
  static constexpr auto kEchoClient = Moniker{"echo_client"};
  static constexpr auto kEchoServer = Moniker{"echo_server"};

  MockEchoClient mock_echo_client(loop());
  auto realm_builder = Realm::Builder::New(context());
  realm_builder.AddComponent(kEchoClient, Component{
                                              .source = Mock{&mock_echo_client},
                                              .eager = true,
                                          });
  realm_builder.AddComponent(kEchoServer, Component{.source = ComponentUrl{kEchoServerUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{test::placeholders::Echo::Name_},
                                         .source = kEchoServer,
                                         .targets = {kEchoClient}});
  auto realm = realm_builder.Build(dispatcher());
  loop().Run();
  EXPECT_TRUE(mock_echo_client.WasCalled());
}

TEST_F(RealmBuilderTest, UsesRandomChildName) {
  std::string child_name_1 = "";
  {
    auto realm_builder = Realm::Builder::New(context());
    auto realm = realm_builder.Build(dispatcher());
    child_name_1 = realm.GetChildName();
  }
  std::string child_name_2 = "";
  {
    auto realm_builder = Realm::Builder::New(context());
    auto realm = realm_builder.Build(dispatcher());
    child_name_2 = realm.GetChildName();
  }

  EXPECT_NE(child_name_1, child_name_2);
}

TEST_F(RealmBuilderTest, PanicsWhenBuildCalledMultipleTimes) {
  ASSERT_DEATH(
      {
        auto realm_builder = Realm::Builder::New(context());
        realm_builder.Build(dispatcher());
        realm_builder.Build(dispatcher());
      },
      "");
}

TEST(RealmBuilderUnittest, PanicsIfMonikerIsBad) {
  auto context = sys::ComponentContext::Create();
  ASSERT_DEATH(
      {
        auto realm_builder = Realm::Builder::New(context.get());
        realm_builder.AddComponent(Moniker{""}, Component{
                                                    .source = ComponentUrl{},
                                                });
      },
      "");
  ASSERT_DEATH(
      {
        auto realm_builder = Realm::Builder::New(context.get());
        realm_builder.AddComponent(Moniker{"/no_leading_slash"}, Component{
                                                                     .source = ComponentUrl{},
                                                                 });
      },
      "");

  ASSERT_DEATH(
      {
        auto realm_builder = Realm::Builder::New(context.get());
        realm_builder.AddComponent(Moniker{"no_trailing_slash/"}, Component{
                                                                      .source = ComponentUrl{},
                                                                  });
      },
      "");
}

TEST(RealmBuilderUnittest, PanicsWhenArgsAreNullptr) {
  ASSERT_DEATH({ Realm::Builder::New(nullptr); }, "");
  ASSERT_DEATH(
      {
        auto context = sys::ComponentContext::Create();
        auto realm_builder = Realm::Builder::New(context.get());
        // Should panic because |async_get_default_dispatcher| was not configured
        // to return nullptr.
        realm_builder.Build(nullptr);
      },
      "");
}

}  // namespace
