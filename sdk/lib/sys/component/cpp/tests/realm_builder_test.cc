// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/data/cpp/fidl.h>
#include <fuchsia/examples/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/comparison.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/optional.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/component/cpp/tests/utils.h>
#include <lib/sys/cpp/component_context.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>
#include <src/lib/fostr/fidl/fuchsia/component/decl/formatting.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>
#include <test/placeholders/cpp/fidl.h>

namespace {

using namespace component_testing;

namespace fdecl = fuchsia::component::decl;

constexpr char kEchoServerUrl[] =
    "fuchsia-pkg://fuchsia.com/component_cpp_testing_realm_builder_tests#meta/echo_server.cm";
constexpr char kEchoServerScUrl[] = "#meta/echo_server_sc.cm";
constexpr char kEchoServerRelativeUrl[] = "#meta/echo_server.cm";
constexpr char kEchoServiceServerUrl[] = "#meta/echo_service_server.cm";

constexpr zx::duration kStopTimeout = zx::msec(500);

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

TEST_F(RealmBuilderTest, PackagedConfigValuesOnly) {
  static constexpr char kEchoServerSc[] = "echo_server_sc";

  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddChild(kEchoServerSc, kEchoServerScUrl);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                               .source = ParentRef(),
                               .targets = {ChildRef{kEchoServerSc}}});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServerSc},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response,
            fidl::StringPtr(
                "hello "
                "[1][255][65535][4000000000][8000000000][-127][-32766][-2000000000][-4000000000]["
                "hello][1,0,][1,2,][2,3,][3,4,][4,5,][-1,-2,][-2,-3,][-3,-4,][-4,-5,][foo,bar,]"));
}

TEST_F(RealmBuilderTest, SetConfigValuesOnly) {
  static constexpr char kEchoServerSc[] = "echo_server_sc";

  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddChild(kEchoServerSc, kEchoServerScUrl);
  realm_builder.InitMutableConfigToEmpty(kEchoServerSc);
  realm_builder.SetConfigValue(kEchoServerSc, "my_flag", ConfigValue::Bool(true));
  realm_builder.SetConfigValue(kEchoServerSc, "my_uint8", ConfigValue::Uint8(1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_uint16", ConfigValue::Uint16(1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_uint32", ConfigValue::Uint32(1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_uint64", ConfigValue::Uint64(1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_int8", ConfigValue::Int8(-1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_int16", ConfigValue::Int16(-1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_int32", ConfigValue::Int32(-1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_int64", ConfigValue::Int64(-1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_string", "foo");
  realm_builder.SetConfigValue(kEchoServerSc, "my_vector_of_flag", std::vector<bool>{false, true});
  realm_builder.SetConfigValue(kEchoServerSc, "my_vector_of_uint8", std::vector<uint8_t>{1, 1});
  realm_builder.SetConfigValue(kEchoServerSc, "my_vector_of_uint16", std::vector<uint16_t>{1, 1});
  realm_builder.SetConfigValue(kEchoServerSc, "my_vector_of_uint32", std::vector<uint32_t>{1, 1});
  realm_builder.SetConfigValue(kEchoServerSc, "my_vector_of_uint64", std::vector<uint64_t>{1, 1});
  realm_builder.SetConfigValue(kEchoServerSc, "my_vector_of_int8", std::vector<int8_t>{-1, 1});
  realm_builder.SetConfigValue(kEchoServerSc, "my_vector_of_int16", std::vector<int16_t>{-1, 1});
  realm_builder.SetConfigValue(kEchoServerSc, "my_vector_of_int32", std::vector<int32_t>{-1, 1});
  realm_builder.SetConfigValue(kEchoServerSc, "my_vector_of_int64", std::vector<int64_t>{-1, 1});
  realm_builder.SetConfigValue(kEchoServerSc, "my_vector_of_string",
                               std::vector<std::string>{"bar", "foo"});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServerSc},
                               .targets = {ParentRef()}});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                               .source = ParentRef(),
                               .targets = {ChildRef{kEchoServerSc}}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello "
                                      "[1][1][1][1][1][-1][-1][-1][-1][foo][0,1,][1,1,][1,1,][1,1,]"
                                      "[1,1,][-1,1,][-1,1,][-1,1,][-1,1,][bar,foo,]"));
}

TEST_F(RealmBuilderTest, MixPackagedAndSetConfigValues) {
  static constexpr char kEchoServerSc[] = "echo_server_sc";

  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddChild(kEchoServerSc, kEchoServerScUrl);
  realm_builder.InitMutableConfigFromPackage(kEchoServerSc);
  realm_builder.SetConfigValue(kEchoServerSc, "my_flag", ConfigValue::Bool(true));
  realm_builder.SetConfigValue(kEchoServerSc, "my_uint8", ConfigValue::Uint8(1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_uint16", ConfigValue::Uint16(1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_uint32", ConfigValue::Uint32(1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_uint64", ConfigValue::Uint64(1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_int8", ConfigValue::Int8(-1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_int16", ConfigValue::Int16(-1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_int32", ConfigValue::Int32(-1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_int64", ConfigValue::Int64(-1));
  realm_builder.SetConfigValue(kEchoServerSc, "my_string", "foo");
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServerSc},
                               .targets = {ParentRef()}});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                               .source = ParentRef(),
                               .targets = {ChildRef{kEchoServerSc}}});
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello "
                                      "[1][1][1][1][1][-1][-1][-1][-1][foo][1,0,][1,2,][2,3,][3,4,]"
                                      "[4,5,][-1,-2,][-2,-3,][-3,-4,][-4,-5,][foo,bar,]"));
}

TEST_F(RealmBuilderTest, SetConfigValueFails) {
  ASSERT_DEATH(
      {
        static constexpr char kEchoServer[] = "echo_server";
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddChild(kEchoServer, kEchoServerRelativeUrl);
        realm_builder.SetConfigValue(kEchoServer, "my_flag", ConfigValue::Bool(true));
      },
      "");
  ASSERT_DEATH(
      {
        static constexpr char kEchoServerSc[] = "echo_server_sc";
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddChild(kEchoServerSc, kEchoServerScUrl);
        realm_builder.SetConfigValue(kEchoServerSc, "doesnt_exist", ConfigValue::Bool(true));
      },
      "");
  ASSERT_DEATH(
      {
        static constexpr char kEchoServerSc[] = "echo_server_sc";
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddChild(kEchoServerSc, kEchoServerScUrl);
        realm_builder.SetConfigValue(kEchoServerSc, "my_string", ConfigValue::Bool(true));
      },
      "");
  ASSERT_DEATH(
      {
        static constexpr char kEchoServerSc[] = "echo_server_sc";
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddChild(kEchoServerSc, kEchoServerScUrl);
        realm_builder.SetConfigValue(kEchoServerSc, "my_string", "abccdefghijklmnop");
      },
      "");
  ASSERT_DEATH(
      {
        static constexpr char kEchoServerSc[] = "echo_server_sc";
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddChild(kEchoServerSc, kEchoServerScUrl);
        realm_builder.SetConfigValue(kEchoServerSc, "my_string",
                                     std::vector<std::string>{"abcdefghijklmnopqrstuvwxyz", "abc"});
      },
      "");
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
  explicit LocalEchoServer(async_dispatcher_t* dispatcher, fit::closure on_start = nullptr,
                           fit::closure on_destruct = nullptr, bool exit_after_serve = false)
      : dispatcher_(dispatcher),
        on_start_(std::move(on_start)),
        on_destruct_(std::move(on_destruct)),
        exit_after_serve_(exit_after_serve),
        called_(false) {}

  explicit LocalEchoServer(fit::closure quit_loop, async_dispatcher_t* dispatcher)
      : quit_loop_(std::move(quit_loop)), dispatcher_(dispatcher), called_(false) {}

  ~LocalEchoServer() override {
    if (on_destruct_) {
      on_destruct_();
    }
  }

  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override {
    callback(std::move(value));
    called_ = true;
    if (quit_loop_) {
      quit_loop_();
    } else if (exit_after_serve_) {
      handles_->Exit(ZX_ERR_CANCELED);
    }
  }

  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    if (on_start_) {
      on_start_();
    }
    handles_ = std::move(handles);
    ASSERT_EQ(handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_)),
              ZX_OK);
  }

  bool WasCalled() const { return called_; }

 private:
  fit::closure quit_loop_;
  async_dispatcher_t* dispatcher_;
  fit::closure on_start_;
  fit::closure on_destruct_;
  fidl::BindingSet<test::placeholders::Echo> bindings_;
  bool exit_after_serve_;
  bool called_;
  std::unique_ptr<LocalComponentHandles> handles_;
};

// Tests and demonstrates that the deprecated AddLocalComponent(LocalComponent*)
// still works.
//
// The Realm does not manage the lifecycle of LocalComponents added by raw
// pointer, which means the API cannot assume the pointer is always valid, and
// in many existing use cases, the pointer does become invalid while the
// Realm is still active.
//
// A mandatory assumption is that the pointer is valid when the Realm is built
// and calls LocalComponent::Start(). After that, RealmBuilder will not
// interact with the component.
//
// The component cannot be restarted.
TEST_F(RealmBuilderTest, RoutesProtocolFromLocalComponentRawPointer) {
  static constexpr char kEchoServer[] = "echo_server";
  LocalEchoServer local_echo_server(QuitLoopClosure(), dispatcher());
  auto realm_builder = RealmBuilder::Create();
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  realm_builder.AddLocalChild(kEchoServer, &local_echo_server);
#pragma clang diagnostic pop
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  test::placeholders::EchoPtr echo;
  ASSERT_EQ(realm.Connect(echo.NewRequest()), ZX_OK);
  echo->EchoString("hello", [](fidl::StringPtr response) { ASSERT_EQ(response, "hello"); });

  RunLoop();
  EXPECT_TRUE(local_echo_server.WasCalled());
}

// Demonstrates the recommended pattern for implementing a restartable
// LocalComponent. A new LocalComponent is returned when requested. After
// instance initiallization, the LocalComponentRunner calls `Start()`, where
// the instance should capture the handles and create and store the bindings.
//
// The component instance will continue to serve requests until the component
// is stopped, or until it is destroyed with the Realm.
TEST_F(RealmBuilderTest, RoutesProtocolUsesLocalComponentFactory) {
  static constexpr char kEchoServer[] = "echo_server";
  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddLocalChild(
      kEchoServer, [&]() { return std::make_unique<LocalEchoServer>(dispatcher()); },
      ChildOptions{});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  test::placeholders::EchoPtr echo;
  ASSERT_EQ(realm.Connect(echo.NewRequest()), ZX_OK);
  echo->EchoString("hello", [&](fidl::StringPtr response) {
    ASSERT_EQ(response, "hello");
    QuitLoop();
  });

  RunLoop();
}

TEST_F(RealmBuilderTest, ComponentCanStopAndBeRestarted) {
  static constexpr char kEchoServer[] = "echo_server";
  auto realm_builder = RealmBuilder::Create();
  bool started = false;
  bool got_response = false;
  bool destructed = false;
  realm_builder.AddLocalChild(kEchoServer, [&]() {
    return std::make_unique<LocalEchoServer>(
        dispatcher(), /*on_start=*/[&started]() { started = true; },
        /*on_destruct=*/[&destructed]() { destructed = true; }, /*exit_after_serve=*/true);
  });
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  test::placeholders::EchoPtr echo;
  ASSERT_EQ(realm.Connect(echo.NewRequest()), ZX_OK);
  echo->EchoString("hello", [&](fidl::StringPtr response) {
    ASSERT_EQ(response, "hello");
    got_response = true;
  });

  RunLoopUntil([&]() { return started && got_response && destructed; });

  // reset the flags
  started = false;
  got_response = false;
  destructed = false;

  // The component destructed, but it will start up again when another request
  // is made.
  ASSERT_EQ(realm.Connect(echo.NewRequest()), ZX_OK);
  echo->EchoString("You're back!", [&](fidl::StringPtr response) {
    ASSERT_EQ(response, "You're back!");
    got_response = true;
  });

  RunLoopUntil([&]() { return started && got_response && destructed; });
}

TEST_F(RealmBuilderTest, RawPointerComponentCannotBeRestarted) {
  static constexpr char kEchoServer[] = "echo_server";
  auto realm_builder = RealmBuilder::Create();
  bool started = false;
  bool got_response = false;
  LocalEchoServer echo_server(
      dispatcher(), /*on_start=*/[&started]() { started = true; }, /*on_destruct=*/nullptr,
      /*exit_after_serve=*/true);
  realm_builder.AddLocalChild(kEchoServer, &echo_server);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  test::placeholders::EchoPtr echo;
  ASSERT_EQ(realm.Connect(echo.NewRequest()), ZX_OK);
  echo->EchoString("hello", [&](fidl::StringPtr response) {
    ASSERT_EQ(response, "hello");
    got_response = true;
  });

  RunLoopUntil([&]() { return started && got_response; });

  RunLoopUntilIdle();

  // reset the flags
  started = false;
  got_response = false;

  // Try to make another request, but since exit_after_server was true,
  // the component should no longer be available to the LocalComponentRunner.
  ASSERT_EQ(realm.Connect(echo.NewRequest()), ZX_OK);
  echo->EchoString("You're back!",
                   [&](fidl::StringPtr response) { ASSERT_EQ(response, "You're back!"); });

  ASSERT_DEATH({ RunLoop(); },
               "Component manager requested a named LocalComponent that is unregistered, already "
               "running, or not restartable.");
}

// Tests and demonstrates a discouraged pattern for calling AddLocalChild()
// that pre-builds a component instance, saves a direct raw pointer, and
// std::moves the instance to the LocalComponentFactory. The instance
// will be returned by the factory when the first component instance starts,
// making the captured instance invalid for subsequent requests to start a new
// instance.
//
// Existing C++ RealmBuilder clients migrating from the deprecated
// AddLocalChild(LocalComponent*) function (which only supported a single
// instance) may want to follow this pattern for simpler migration to the
// new AddLocalChild() API.
TEST_F(RealmBuilderTest, RoutesProtocolFromPrebuiltLocalComponentInstance) {
  static constexpr char kEchoServer[] = "echo_server";
  auto local_echo_server = std::make_unique<LocalEchoServer>(QuitLoopClosure(), dispatcher());
  auto local_echo_server_ptr = local_echo_server.get();
  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddLocalChild(
      kEchoServer,
      [&, local_echo_server = std::move(local_echo_server)]() mutable {
        // Note: This lambda does not create a new instance,
        // so the component can only be started once.
        return std::move(local_echo_server);
      },
      ChildOptions{});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ParentRef()}});
  auto realm = realm_builder.Build(dispatcher());
  test::placeholders::EchoPtr echo;
  ASSERT_EQ(realm.Connect(echo.NewRequest()), ZX_OK);
  echo->EchoString("hello", [](fidl::StringPtr _) {});

  RunLoop();
  // The pointer should still be valid, since the realm is still valid and the
  // component was not stopped.
  EXPECT_TRUE(local_echo_server_ptr->WasCalled());
}

class EchoClientSyncLocalComponent : public LocalComponent {
 public:
  explicit EchoClientSyncLocalComponent(fit::closure on_success)
      : on_success_(std::move(on_success)) {}

  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    test::placeholders::EchoSyncPtr echo;
    ASSERT_EQ(handles->svc().Connect<test::placeholders::Echo>(echo.NewRequest()), ZX_OK);
    fidl::StringPtr response;
    ASSERT_EQ(echo->EchoString("milk", &response), ZX_OK);
    ASSERT_EQ(response, fidl::StringPtr("milk"));
    if (on_success_) {
      on_success_();
    }
    handles->Exit();
  }

 private:
  fit::closure on_success_;
};

// Tests and demonstrates creating a local client-only component that can
// terminate immediately, after completing its work.
//
// Note: This client component uses a `SyncPtr` proxy binding, which invokes
// the call and (in the case of Echo::EchoString()) waits for the response
// synchronously.
TEST_F(RealmBuilderTest, RoutesProtocolToLocalComponentSync) {
  static constexpr char kEchoClient[] = "echo_client";
  static constexpr char kEchoServer[] = "echo_server";

  auto realm_builder = RealmBuilder::Create();
  bool success = false;
  realm_builder.AddLocalChild(
      kEchoClient,
      [&success]() {
        return std::make_unique<EchoClientSyncLocalComponent>([&success]() { success = true; });
      },
      ChildOptions{.startup_mode = StartupMode::EAGER});
  realm_builder.AddChild(kEchoServer, kEchoServerUrl);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ChildRef{kEchoClient}}});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                               .source = ParentRef(),
                               .targets = {ChildRef{kEchoServer}}});
  auto realm = realm_builder.Build(dispatcher());
  RunLoopUntil([&success]() { return success; });
}

class EchoClientLocalComponent : public LocalComponent {
 public:
  explicit EchoClientLocalComponent(fit::closure on_success) : on_success_(std::move(on_success)) {}

  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    handles_ = std::move(handles);
    ASSERT_EQ(handles_->svc().Connect<test::placeholders::Echo>(echo_.NewRequest()), ZX_OK);
    echo_->EchoString("hello", [&](const fidl::StringPtr& response) {
      ASSERT_EQ("hello", response);
      if (on_success_) {
        on_success_();
      }
      handles_->Exit();
    });
  }

 private:
  std::unique_ptr<LocalComponentHandles> handles_;
  test::placeholders::EchoPtr echo_;
  fit::closure on_success_;
};

// Tests and demonstrates creating a local client-only component that uses
// an asynchronous proxy binding to call a service and get a response. Since
// the call is asynchronous, the component must not call `handles->Exit()`
// until it gets a response.
TEST_F(RealmBuilderTest, RoutesProtocolToLocalComponentAsync) {
  static constexpr char kEchoClient[] = "echo_client";
  static constexpr char kEchoServer[] = "echo_server";

  auto realm_builder = RealmBuilder::Create();
  bool success = false;
  realm_builder.AddLocalChild(
      kEchoClient,
      [&success]() {
        return std::make_unique<EchoClientLocalComponent>([&success]() { success = true; });
      },
      ChildOptions{.startup_mode = StartupMode::EAGER});
  realm_builder.AddChild(kEchoServer, kEchoServerUrl);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ChildRef{kEchoClient}}});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                               .source = ParentRef(),
                               .targets = {ChildRef{kEchoServer}}});
  auto realm = realm_builder.Build(dispatcher());
  RunLoopUntil([&success]() { return success; });
}

class EchoClientLocalComponentDropsHandles : public LocalComponent {
 public:
  explicit EchoClientLocalComponentDropsHandles(fit::closure on_success)
      : on_success_(std::move(on_success)) {}

  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    test::placeholders::EchoPtr echo;
    ASSERT_EQ(handles->svc().Connect<test::placeholders::Echo>(echo.NewRequest()), ZX_OK);
    echo->EchoString("hello", [&](const fidl::StringPtr& response) {
      ASSERT_EQ("hello", response);
      if (on_success_) {
        on_success_();
      }
      handles->Exit();
    });
  }

 private:
  fit::closure on_success_;
};

// Tests and demonstrates creating a local client-only component that should
// continue running after `Start()` but if it doesn't capture the
// LocalComponentHandles, the RealmBuilder API will fail an assertion.
TEST_F(RealmBuilderTest, CrashesIfLocalComponentDropsHandlesWhileRunning) {
  static constexpr char kEchoClient[] = "echo_client";
  static constexpr char kEchoServer[] = "echo_server";

  auto realm_builder = RealmBuilder::Create();
  bool success = false;
  realm_builder.AddLocalChild(
      kEchoClient,
      [&success]() {
        return std::make_unique<EchoClientLocalComponentDropsHandles>(
            [&success]() { success = true; });
      },
      ChildOptions{.startup_mode = StartupMode::EAGER});
  realm_builder.AddChild(kEchoServer, kEchoServerUrl);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kEchoServer},
                               .targets = {ChildRef{kEchoClient}}});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                               .source = ParentRef(),
                               .targets = {ChildRef{kEchoServer}}});
  auto realm = realm_builder.Build(dispatcher());
  ASSERT_DEATH({ RunLoopUntil([&success]() { return success; }); },
               R"(The LocalComponent::Start\(\) method must save the LocalComponentHandles)");
}

TEST_F(RealmBuilderTest, RoutesServiceFromChild) {
  static constexpr char kEchoServiceServer[] = "echo_service_server";

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

TEST_F(RealmBuilderTest, RoutesProtocolFromLocalComponentInSubRealm) {
  static constexpr char kEchoServer[] = "echo_server";
  static constexpr char kSubRealm[] = "sub_realm";
  auto realm_builder = RealmBuilder::Create();
  auto sub_realm = realm_builder.AddChildRealm(kSubRealm);

  // Route test.placeholders.Echo from local Echo server impl to parent.
  sub_realm.AddLocalChild(kEchoServer,
                          [&]() { return std::make_unique<LocalEchoServer>(dispatcher()); });
  sub_realm.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                           .source = ChildRef{kEchoServer},
                           .targets = {ParentRef()}});

  // Route test.placeholders.Echo from sub_realm child to parent.
  realm_builder.AddRoute(Route{.capabilities = {Protocol{test::placeholders::Echo::Name_}},
                               .source = ChildRef{kSubRealm},
                               .targets = {ParentRef()}});

  auto realm = realm_builder.Build(dispatcher());
  test::placeholders::EchoPtr echo;
  ASSERT_EQ(realm.Connect(echo.NewRequest()), ZX_OK);
  echo->EchoString("hello", [&](const fidl::StringPtr& response) {
    ASSERT_EQ("hello", response);
    QuitLoop();
  });
  RunLoop();
}

class FileReader : public LocalComponent {
 public:
  explicit FileReader(fit::closure quit_loop) : quit_loop_(std::move(quit_loop)) {}

  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    handles_ = std::move(handles);
    started_ = true;
    quit_loop_();
  }

  std::string GetContentsAt(std::string_view dirpath, std::string_view filepath) {
    ZX_ASSERT_MSG(handles_ != nullptr,
                  "FileReader/GetContentsAt called before FileReader was started.");

    constexpr static size_t kMaxBufferSize = 1024;
    static char kReadBuffer[kMaxBufferSize];

    int dirfd = fdio_ns_opendir(handles_->ns());
    ZX_ASSERT_MSG(dirfd > 0, "Failed to open root ns as a file descriptor: %s", strerror(errno));

    std::stringstream path_builder;
    path_builder << dirpath << '/' << filepath;
    auto path = path_builder.str();
    int filefd = openat(dirfd, path.c_str(), O_RDONLY);
    ZX_ASSERT_MSG(filefd > 0, "Failed to open path \"%s\": %s", path.c_str(), strerror(errno));

    size_t const bytes_read = read(filefd, reinterpret_cast<void*>(kReadBuffer), kMaxBufferSize);
    ZX_ASSERT_MSG(bytes_read > 0, "Read 0 bytes from file at \"%s\": %s", path.c_str(),
                  strerror(errno));

    return std::string(kReadBuffer, bytes_read);
  }

  bool HasStarted() const { return started_; }

 private:
  fit::closure quit_loop_;
  bool started_ = false;
  std::unique_ptr<LocalComponentHandles> handles_;
};

TEST_F(RealmBuilderTest, RoutesReadOnlyDirectory) {
  static constexpr char kDirectoryName[] = "config";
  static constexpr char kFilename[] = "environment";
  static constexpr char kContent[] = "DEV";

  auto realm_builder = RealmBuilder::Create();
  FileReader* file_reader_ptr = nullptr;
  realm_builder.AddLocalChild(
      "file_reader",
      [this, &file_reader_ptr]() {
        auto file_reader = std::make_unique<FileReader>(QuitLoopClosure());
        file_reader_ptr = file_reader.get();
        return file_reader;
      },
      ChildOptions{.startup_mode = StartupMode::EAGER});
  realm_builder.RouteReadOnlyDirectory(kDirectoryName, {ChildRef{"file_reader"}},
                                       std::move(DirectoryContents().AddFile(kFilename, kContent)));
  auto realm = realm_builder.Build(dispatcher());

  RunLoop();

  ASSERT_TRUE(file_reader_ptr);
  ASSERT_TRUE(file_reader_ptr->HasStarted());
  EXPECT_EQ(file_reader_ptr->GetContentsAt(kDirectoryName, kFilename), kContent);
}

// This test is similar to RealmBuilderTest.RoutesProtocolFromChild except
// that its setup is done by mutating the realm's root's decl. This is to
// assert that invoking |ReplaceRealmDecl| works as expected.
TEST_F(RealmBuilderTest, RealmDeclCanBeReplaced) {
  static constexpr char kEchoServer[] = "echo_server";

  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddChild(kEchoServer, kEchoServerUrl);

  auto decl = realm_builder.GetRealmDecl();
  fdecl::ExposeProtocol expose_protocol;
  expose_protocol.set_source(fdecl::Ref::WithChild(fdecl::ChildRef{.name = "echo_server"}));
  expose_protocol.set_target(fdecl::Ref::WithParent(fdecl::ParentRef{}));
  expose_protocol.set_source_name("test.placeholders.Echo");
  expose_protocol.set_target_name("test.placeholders.Echo");
  decl.mutable_exposes()->emplace_back(fdecl::Expose::WithProtocol(std::move(expose_protocol)));
  realm_builder.ReplaceRealmDecl(std::move(decl));
  auto realm = realm_builder.Build(dispatcher());

  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

// This test is similar to RealmBuilderTest.RoutesProtocolFromChild except
// that its setup is done statically via a manifest. This is to assert that
// invoking |CreateFromRelativeUrl| works as expected.
TEST_F(RealmBuilderTest, BuildsRealmFromRelativeUrl) {
  static constexpr char kPrePopulatedRealmUrl[] = "#meta/pre_populated_realm.cm";

  auto realm_builder = RealmBuilder::CreateFromRelativeUrl(kPrePopulatedRealmUrl);
  auto realm = realm_builder.Build(dispatcher());
  auto echo = realm.ConnectSync<test::placeholders::Echo>();
  fidl::StringPtr response;
  ASSERT_EQ(echo->EchoString("hello", &response), ZX_OK);
  EXPECT_EQ(response, fidl::StringPtr("hello"));
}

class SimpleComponent : public component_testing::LocalComponent {
 public:
  SimpleComponent() = default;
  explicit SimpleComponent(fit::closure on_destruct)
      : on_destruct_(std::make_unique<fit::closure>(std::move(on_destruct))) {}

  ~SimpleComponent() override {
    if (on_destruct_) {
      (*on_destruct_)();
    }
  }

  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    handles_ = std::move(handles);
    started_ = true;
  }

  void Stop() override { stopping_ = true; }

  bool IsStarted() const { return started_; }

  bool IsStopping() const { return stopping_; }

 private:
  bool started_ = false;
  bool stopping_ = false;
  std::unique_ptr<fit::closure> on_destruct_;
  std::unique_ptr<LocalComponentHandles> handles_;
};

// This test asserts that the LocalComponents are started, not stopped, and
// are eventually destructed when destructing the realm.
TEST_F(RealmBuilderTest, LocalComponentGetsDestructedOnExit) {
  auto realm_builder = RealmBuilder::Create();

  size_t destructors_called = 0;

  // hold pointers to the LocalComponents owned by the realm
  std::vector<SimpleComponent*> components;
  for (size_t i = 0; i < 3; ++i) {
    std::string name = "numbered" + std::to_string(i);
    auto component = std::make_unique<SimpleComponent>([&]() { destructors_called++; });
    components.push_back(component.get());
    realm_builder.AddLocalChild(
        name, [component = std::move(component)]() mutable { return std::move(component); },
        ChildOptions{.startup_mode = StartupMode::EAGER});
  }

  auto realm = std::make_optional<RealmRoot>(realm_builder.Build(dispatcher()));
  for (auto& component : components) {
    ASSERT_FALSE(component->IsStarted());
    ASSERT_FALSE(component->IsStopping());
  }

  // Verify all components have started.
  for (auto& component : components) {
    RunLoopUntil([&]() { return component->IsStarted(); });
  }

  for (auto& component : components) {
    ASSERT_FALSE(component->IsStopping());
  }

  ASSERT_EQ(destructors_called, 0u);

  // drop all component instances
  realm.reset();

  ASSERT_EQ(destructors_called, components.size());
}

// This test asserts that the LocalComponent::Stop() method is called when
// the component is stopped (which confirms that the ComponentController
// would have also been dropped).
TEST_F(RealmBuilderTest, LocalComponentGetsStop) {
  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::sys2::LifecycleController::Name_}},
            .source = FrameworkRef(),
            .targets = {ParentRef{}}});

  size_t destructors_called = 0;

  std::vector<SimpleComponent*> components;
  for (size_t i = 0; i < 3; ++i) {
    std::string name = "numbered" + std::to_string(i);
    auto component = std::make_unique<SimpleComponent>([&]() { destructors_called++; });
    components.push_back(component.get());
    realm_builder.AddLocalChild(
        name, [component = std::move(component)]() mutable { return std::move(component); },
        ChildOptions{.startup_mode = StartupMode::EAGER});
  }

  auto realm = std::make_optional<RealmRoot>(realm_builder.Build(dispatcher()));
  for (auto& component : components) {
    ASSERT_FALSE(component->IsStarted());
    ASSERT_FALSE(component->IsStopping());
  }

  // Verify all components have started.
  for (auto& component : components) {
    RunLoopUntil([&]() { return component->IsStarted(); });
  }

  for (auto& component : components) {
    ASSERT_FALSE(component->IsStopping());
  }

  // Stop the components and verify all components have been asked to stop.
  auto lifecycle_controller = realm->Connect<fuchsia::sys2::LifecycleController>();
  for (size_t i = 0; i < components.size(); ++i) {
    size_t orig = destructors_called;
    std::string moniker = "./numbered" + std::to_string(i);
    lifecycle_controller->Stop(moniker, false,
                               [](auto result) { ASSERT_TRUE(result.is_response()); });
    RunLoopUntil([&]() { return destructors_called == orig + 1; });
  }
}

// This test asserts that the same test that invokes LocalComponent::Stop()
// does not invoke Stop() if the LocalComponent was added via raw pointer.
// This behavior is required because the pointer is not guaranteed to be valid,
// though we assume it is valid long enough to call Start(), since existing
// uses of the now-deprecated AddLocalChild(LocalComponent*) currently work.
TEST_F(RealmBuilderTest, RawPtrLocalChildDoesNotGetStop) {
  auto realm_builder = RealmBuilder::Create();

  realm_builder.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::sys2::LifecycleController::Name_}},
            .source = FrameworkRef(),
            .targets = {ParentRef{}}});

  std::vector<SimpleComponent> components(3);
  for (size_t i = 0; i < components.size(); ++i) {
    std::string name = "numbered" + std::to_string(i);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    realm_builder.AddLocalChild(name, &components[i],
                                ChildOptions{.startup_mode = StartupMode::EAGER});
#pragma clang diagnostic pop
  }

  auto realm = std::make_optional<RealmRoot>(realm_builder.Build(dispatcher()));
  for (const auto& component : components) {
    ASSERT_FALSE(component.IsStarted());
    ASSERT_FALSE(component.IsStopping());
  }

  // Verify all components have started.
  for (const auto& component : components) {
    RunLoopUntil([&]() { return component.IsStarted(); });
  }

  for (const auto& component : components) {
    ASSERT_FALSE(component.IsStopping());
  }

  // Stop the components and verify all components have been asked to stop.
  auto lifecycle_controller = realm->Connect<fuchsia::sys2::LifecycleController>();
  for (size_t i = 0; i < components.size(); ++i) {
    std::string const moniker = "./numbered" + std::to_string(i);
    lifecycle_controller->Stop(moniker, false,
                               [](auto result) { ASSERT_TRUE(result.is_response()); });
    RunLoopWithTimeoutOrUntil([&]() { return components[i].IsStopping(); }, kStopTimeout);
    ASSERT_FALSE(components[i].IsStopping());
  }
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

TEST_F(RealmBuilderTest, CanCreateLongChildName) {
  std::string child_name_1;
  {
    auto realm_builder = RealmBuilder::Create();
    {
      const std::string long_child_name(fuchsia::component::MAX_NAME_LENGTH + 1, 'a');
      // AddChild should not panic.
      realm_builder.AddChild(long_child_name, kEchoServerUrl);
    }
    {
      const std::string long_child_name(fuchsia::component::MAX_CHILD_NAME_LENGTH, 'a');
      // AddChild should not panic.
      realm_builder.AddChild(long_child_name, kEchoServerUrl);
    }
    {
      ASSERT_DEATH(
          {
            const std::string too_long_child_name(fuchsia::component::MAX_CHILD_NAME_LENGTH + 1,
                                                  'a');
            realm_builder.AddChild(too_long_child_name, kEchoServerUrl);
          },
          "");
    }
  }
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

  class BasicLocalImpl : public LocalComponent {
    void Start(std::unique_ptr<LocalComponentHandles> /* mock_handles */) override {}
  };
  ASSERT_DEATH(
      {
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddLocalChild("",
                                    []() -> std::unique_ptr<LocalComponent> { return nullptr; });
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  ASSERT_DEATH(
      {
        auto realm_builder = RealmBuilder::Create();
        realm_builder.AddLocalChild("some_valid_name", nullptr);
      },
      "");
#pragma clang diagnostic pop
}

TEST(DirectoryContentsUnittest, PanicWhenGivenInvalidPath) {
  ASSERT_DEATH(
      {
        auto directory_contents = DirectoryContents();
        directory_contents.AddFile("/foo/bar.txt", "Hello World!");
      },
      "");

  ASSERT_DEATH(
      {
        auto directory_contents = DirectoryContents();
        directory_contents.AddFile("foo/bar/", "Hello World!");
      },
      "");

  ASSERT_DEATH(
      {
        auto directory_contents = DirectoryContents();
        directory_contents.AddFile("", "Hello World!");
      },
      "");
}

class PlaceholderComponent : public LocalComponent {
 public:
  PlaceholderComponent() = default;

  void Start(std::unique_ptr<LocalComponentHandles> handles) override {}
};

constexpr char kRoutingTestChildName[] = "foobar";

class RealmBuilderRoutingParameterizedFixture
    : public testing::TestWithParam<std::pair<Capability, std::shared_ptr<fdecl::Offer>>> {};

TEST_P(RealmBuilderRoutingParameterizedFixture, RoutedCapabilitiesYieldExpectedOfferClauses) {
  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddLocalChild(kRoutingTestChildName,
                              []() { return std::make_unique<PlaceholderComponent>(); });

  auto param = GetParam();
  auto capability = param.first;
  realm_builder.AddRoute(Route{.capabilities = {capability},
                               .source = ParentRef{},
                               .targets = {ChildRef{kRoutingTestChildName}}});

  auto root_decl = realm_builder.GetRealmDecl();

  ASSERT_EQ(root_decl.offers().size(), 1ul);

  const fdecl::Offer& actual = root_decl.offers().at(0);
  const fdecl::Offer& expected = *param.second;

  EXPECT_TRUE(fidl::Equals(actual, expected)) << "Actual: " << actual << std::endl
                                              << "Expected: " << expected << std::endl;
}

INSTANTIATE_TEST_SUITE_P(
    RealmBuilderRoutingTest, RealmBuilderRoutingParameterizedFixture,
    testing::Values(
        std::make_pair(Protocol{.name = "foo", .as = "bar"},
                       component::tests::CreateFidlProtocolOfferDecl(
                           /*source_name=*/"foo",
                           /*source=*/component::tests::CreateFidlParentRef(),
                           /*target_name=*/"bar",
                           /*target=*/component::tests::CreateFidlChildRef(kRoutingTestChildName))),
        std::make_pair(Service{.name = "foo", .as = "bar"},
                       component::tests::CreateFidlServiceOfferDecl(
                           /*source_name=*/"foo",
                           /*source=*/component::tests::CreateFidlParentRef(),
                           /*target_name=*/"bar",
                           /*target=*/component::tests::CreateFidlChildRef(kRoutingTestChildName))),
        std::make_pair(Directory{.name = "foo",
                                 .as = "bar",
                                 .subdir = "sub",
                                 .rights = fuchsia::io::RW_STAR_DIR,
                                 .path = "/foo"},
                       component::tests::CreateFidlDirectoryOfferDecl(
                           /*source_name=*/"foo",
                           /*source=*/component::tests::CreateFidlParentRef(),
                           /*target_name=*/"bar",
                           /*target=*/component::tests::CreateFidlChildRef(kRoutingTestChildName),
                           /*subdir=*/"sub",
                           /*rights=*/fuchsia::io::RW_STAR_DIR)),
        std::make_pair(
            Storage{.name = "foo", .as = "bar", .path = "/foo"},
            component::tests::CreateFidlStorageOfferDecl(
                /*source_name=*/"foo", /*source=*/component::tests::CreateFidlParentRef(),
                /*target_name=*/"bar",
                /*target=*/component::tests::CreateFidlChildRef(kRoutingTestChildName)))));
}  // namespace
