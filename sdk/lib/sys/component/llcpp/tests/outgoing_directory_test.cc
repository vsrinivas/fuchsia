// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples/cpp/wire.h>
#include <fidl/fuchsia.examples/cpp/wire_messaging.h>
#include <fidl/fuchsia.io/cpp/markers.h>
#include <fidl/fuchsia.io/cpp/natural_types.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/internal/transport_channel.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/service/llcpp/service.h>
#include <lib/stdcompat/string_view.h>
#include <lib/sys/component/llcpp/constants.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

namespace {

// Expected path of directory hosting FIDL Services & Protocols.
constexpr char kSvcDirectoryPath[] = "svc";

constexpr char kTestString[] = "FizzBuzz";
constexpr char kTestStringReversed[] = "zzuBzziF";

class EchoImpl final : public fidl::WireServer<fuchsia_examples::Echo> {
 public:
  explicit EchoImpl(bool reversed) : reversed_(reversed) {}

  void SendString(SendStringRequestView request, SendStringCompleter::Sync& completer) override {}

  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    std::string value(request->value.get());
    if (reversed_) {
      std::reverse(value.begin(), value.end());
    }
    auto reply = fidl::StringView::FromExternal(value);
    completer.Reply(reply);
  }

 private:
  bool reversed_ = false;
};

class OutgoingDirectoryTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    outgoing_directory_ = std::make_unique<component_llcpp::OutgoingDirectory>();
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ZX_ASSERT(outgoing_directory_->Serve(std::move(endpoints->server), dispatcher()).is_ok());
    svc_client_ =
        fidl::WireClient<fuchsia_io::Directory>(std::move(endpoints->client), dispatcher());

    async_set_default_dispatcher(dispatcher());
  }

  component_llcpp::OutgoingDirectory* GetOutgoingDirectory() { return outgoing_directory_.get(); }

  fidl::ClientEnd<fuchsia_io::Directory> TakeSvcClientEnd() {
    zx::channel server_end, client_end;
    ZX_ASSERT(ZX_OK == zx::channel::create(0, &server_end, &client_end));
    svc_client_->Open(fuchsia_io::OPEN_RIGHT_WRITABLE | fuchsia_io::OPEN_RIGHT_READABLE,
                      fuchsia_io::MODE_TYPE_DIRECTORY, kSvcDirectoryPath,
                      fidl::ServerEnd<fuchsia_io::Node>(std::move(server_end)));
    return fidl::ClientEnd<fuchsia_io::Directory>(std::move(client_end));
  }

 protected:
  void InstallServiceHandler(fuchsia_examples::EchoService::Handler& service_handler,
                             EchoImpl* impl, bool reversed) {
    auto handler = [dispatcher = dispatcher(),
                    impl = impl](fidl::ServerEnd<fuchsia_examples::Echo> request) -> zx::status<> {
      // This is invoked during handler unbound. We're not testing for that
      // here so we just provide a no-op callback.
      auto _on_unbound = [](EchoImpl* impl, fidl::UnbindInfo info,
                            fidl::ServerEnd<fuchsia_examples::Echo> server_end) {};
      fidl::BindServer(dispatcher, std::move(request), impl, std::move(_on_unbound));
      return zx::ok();
    };
    auto result = reversed ? service_handler.add_reversed_echo(std::move(handler))
                           : service_handler.add_regular_echo(std::move(handler));
    ZX_ASSERT(result.is_ok());
  }

  fidl::WireClient<fuchsia_examples::Echo> ConnectToServiceMember(
      fuchsia_examples::EchoService::ServiceClient& service, bool reversed) {
    auto connect_result =
        reversed ? service.connect_reversed_echo() : service.connect_regular_echo();
    ZX_ASSERT(connect_result.is_ok());
    return fidl::WireClient<fuchsia_examples::Echo>(std::move(connect_result.value()),
                                                    dispatcher());
  }

 private:
  std::unique_ptr<component_llcpp::OutgoingDirectory> outgoing_directory_ = nullptr;
  fidl::WireClient<fuchsia_io::Directory> svc_client_;
};

// Test that outgoing directory is able to serve multiple service members. In
// this case, the directory will host the `fuchsia.examples.EchoService` which
// contains two `fuchsia.examples.Echo` member. One regular, and one reversed.
TEST_F(OutgoingDirectoryTest, AddServiceServesAllMembers) {
  // Setup service handler.
  component_llcpp::ServiceHandler service_handler;
  fuchsia_examples::EchoService::Handler echo_service_handler(&service_handler);

  // First, install the regular Echo server in this service handler.
  EchoImpl regular_impl(/*reversed=*/false);
  InstallServiceHandler(echo_service_handler, &regular_impl, /*reversed=*/false);

  // Then, install the reverse Echo server. This instance will reverse the string
  // received in calls to EchoString.
  EchoImpl reversed_impl(/*reversed=*/true);
  InstallServiceHandler(echo_service_handler, &reversed_impl, /*reversed=*/true);

  ZX_ASSERT(GetOutgoingDirectory()
                ->AddService<fuchsia_examples::EchoService>(std::move(service_handler))
                .is_ok());

  // Setup test client.
  auto open_result = service::OpenServiceAt<fuchsia_examples::EchoService>(TakeSvcClientEnd());
  ZX_ASSERT(open_result.is_ok());

  fuchsia_examples::EchoService::ServiceClient service = std::move(open_result.value());

  // Assert that service is connected and that proper impl returns expected reply.
  for (bool reversed : {true, false}) {
    bool message_echoed = false;
    auto client = ConnectToServiceMember(service, reversed);
    auto expected_reply = reversed ? kTestStringReversed : kTestString;
    client->EchoString(
        kTestString,
        [quit_loop = QuitLoopClosure(), &message_echoed, expected_reply = expected_reply](
            fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& reply) {
          EXPECT_TRUE(reply.ok()) << "Reply failed with: " << reply.error().status_string();
          EXPECT_EQ(reply.value().response.get(), cpp17::string_view(expected_reply));
          message_echoed = true;
          quit_loop();
        });

    RunLoop();

    EXPECT_TRUE(message_echoed);
  }

  // Next, assert that after removing the service, the client end yields ZX_ERR_PEER_CLOSED.
  ZX_ASSERT(GetOutgoingDirectory()->RemoveService<fuchsia_examples::EchoService>().is_ok());
  for (bool reversed : {true, false}) {
    auto connect_result =
        reversed ? service.connect_reversed_echo() : service.connect_regular_echo();
    ZX_ASSERT(connect_result.is_error());
    EXPECT_EQ(connect_result.status_value(), ZX_ERR_PEER_CLOSED);
  }
}

// Test user errors on |Serve| method.
// These test cases use a local instance of outgoing directory instead of
// the class' instance in order to test |Serve|.
TEST_F(OutgoingDirectoryTest, ServeFailsOnBadInput) {
  // Test invalid directory handle.
  {
    auto outgoing_directory = std::make_unique<component_llcpp::OutgoingDirectory>();
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    // Close server end in order to  invalidate channel.
    endpoints->server.reset();
    EXPECT_EQ(outgoing_directory->Serve(std::move(endpoints->server)).status_value(),
              ZX_ERR_BAD_HANDLE);
  }

  // Test multiple invocations of |Serve|.
  {
    auto outgoing_directory = std::make_unique<component_llcpp::OutgoingDirectory>();
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ZX_ASSERT(outgoing_directory->Serve(std::move(endpoints->server)).is_ok());
    auto fresh_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_EQ(outgoing_directory->Serve(std::move(fresh_endpoints->server)).status_value(),
              ZX_ERR_ALREADY_EXISTS);
  }
}

// Test that serving a FIDL Protocol works as expected.
TEST_F(OutgoingDirectoryTest, AddProtocolCanServeMultipleProtocols) {
  constexpr static std::array<std::pair<bool, const char*>, 2> kIsReversedAndPaths = {
      {{false, "fuchsia.examples.Echo"}, {true, "fuchsia.examples.Ohce"}}};

  // Setup fuchsia.examples.Echo servers
  EchoImpl regular_impl(/*reversed=*/false);
  EchoImpl reversed_impl(/*reversed=*/true);
  for (auto [reversed, path] : kIsReversedAndPaths) {
    auto* impl = reversed ? &reversed_impl : &regular_impl;
    ASSERT_EQ(GetOutgoingDirectory()
                  ->AddProtocol<fuchsia_examples::Echo>(impl, dispatcher(), path)
                  .status_value(),
              ZX_OK);
  }

  // Setup fuchsia.examples.Echo client
  for (auto [reversed, path] : kIsReversedAndPaths) {
    auto client_end = service::ConnectAt<fuchsia_examples::Echo>(TakeSvcClientEnd(), path);
    ASSERT_EQ(client_end.status_value(), ZX_OK);
    fidl::WireClient<fuchsia_examples::Echo> client(std::move(*client_end), dispatcher());

    std::string reply_received;
    client->EchoString(
        kTestString, [&reply_received, quit_loop = QuitLoopClosure()](
                         fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& result) {
          ZX_ASSERT_MSG(result.ok(), "EchoString failed: %s",
                        result.error().FormatDescription().c_str());
          auto* response = result.Unwrap();
          reply_received = std::string(response->response.data(), response->response.size());
          quit_loop();
        });
    RunLoop();

    auto expected_reply = reversed ? kTestStringReversed : kTestString;
    EXPECT_EQ(reply_received, expected_reply);
  }
}

TEST_F(OutgoingDirectoryTest, AddProtocolFailsIfServeNotCalled) {
  auto outgoing_directory = std::make_unique<component_llcpp::OutgoingDirectory>();
  EchoImpl impl(/*reversed=*/false);
  EXPECT_EQ(
      outgoing_directory->AddProtocol<fuchsia_examples::Echo>(&impl, dispatcher()).status_value(),
      ZX_ERR_BAD_HANDLE);
}

TEST_F(OutgoingDirectoryTest, AddProtocolFailsIfImplIsNullptr) {
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddProtocol<fuchsia_examples::Echo>(/*impl=*/nullptr, dispatcher())
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(OutgoingDirectoryTest, AddProtocolFailsIfDispatcherIsNullptr) {
  EchoImpl impl(/*reversed=*/false);
  // Before nullifying the global dispatcher, we'll keep a reference to it
  // in a local variable. This is needed because this test's teardown relies on
  // it being present.
  async_dispatcher_t* dispatcher = async_get_default_dispatcher();

  // Now that it's in a local variable, it's safe to set this to nullptr.
  async_set_default_dispatcher(nullptr);
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddProtocol<fuchsia_examples::Echo>(&impl, /*dispatcher=*/nullptr)
                .status_value(),
            ZX_ERR_INVALID_ARGS);

  // Return original dispatcher before test tear down.
  async_set_default_dispatcher(dispatcher);
}

TEST_F(OutgoingDirectoryTest, RemoveProtocolFailsIfEntryDoesNotExist) {
  EXPECT_EQ(GetOutgoingDirectory()->RemoveProtocol<fuchsia_examples::Echo>().status_value(),
            ZX_ERR_NOT_FOUND);
}

TEST_F(OutgoingDirectoryTest, RemoveProtocolFailsIfServeNotCalled) {
  auto outgoing_directory = std::make_unique<component_llcpp::OutgoingDirectory>();
  component_llcpp::ServiceHandler service_handler;
  EXPECT_EQ(outgoing_directory->RemoveProtocol<fuchsia_examples::Echo>().status_value(),
            ZX_ERR_BAD_HANDLE);
}

TEST_F(OutgoingDirectoryTest, AddServiceFailsIfServeNotCalled) {
  auto outgoing_directory = std::make_unique<component_llcpp::OutgoingDirectory>();
  component_llcpp::ServiceHandler service_handler;
  EXPECT_EQ(
      outgoing_directory->AddService<fuchsia_examples::EchoService>(std::move(service_handler))
          .status_value(),
      ZX_ERR_BAD_HANDLE);
}

TEST_F(OutgoingDirectoryTest, RemoveServiceFailsIfServeNotCalled) {
  auto outgoing_directory = std::make_unique<component_llcpp::OutgoingDirectory>();
  component_llcpp::ServiceHandler service_handler;
  EXPECT_EQ(outgoing_directory->RemoveService<fuchsia_examples::EchoService>().status_value(),
            ZX_ERR_BAD_HANDLE);
}

TEST_F(OutgoingDirectoryTest, RemoveServiceFailsIfEntryDoesNotExist) {
  EXPECT_EQ(GetOutgoingDirectory()->RemoveService<fuchsia_examples::EchoService>().status_value(),
            ZX_ERR_NOT_FOUND);
}

TEST_F(OutgoingDirectoryTest, AddServiceFailsIfServiceHandlerEmpty) {
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddService<fuchsia_examples::EchoService>(component_llcpp::ServiceHandler())
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

class OutgoingDirectoryPathParameterizedFixture
    : public testing::TestWithParam<std::pair<std::string, std::string>> {};

TEST_P(OutgoingDirectoryPathParameterizedFixture, BadServicePaths) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto outgoing_directory = std::make_unique<component_llcpp::OutgoingDirectory>();
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ZX_ASSERT(outgoing_directory->Serve(std::move(endpoints->server)).is_ok());
  component_llcpp::ServiceHandler service_handler;
  fuchsia_examples::EchoService::Handler echo_service_handler(&service_handler);
  EchoImpl regular_impl(/*reversed=*/false);
  auto noop_handler = [](fidl::ServerEnd<fuchsia_examples::Echo> _request) -> zx::status<> {
    return zx::ok();
  };
  ZX_ASSERT(echo_service_handler.add_regular_echo(std::move(noop_handler)).is_ok());

  auto service_and_instance_names = GetParam();
  EXPECT_EQ(
      outgoing_directory
          ->AddNamedService(component_llcpp::ServiceHandler(), service_and_instance_names.first,
                            service_and_instance_names.second)
          .status_value(),
      ZX_ERR_INVALID_ARGS);
}

INSTANTIATE_TEST_SUITE_P(OutgoingDirectoryTestPathTest, OutgoingDirectoryPathParameterizedFixture,
                         testing::Values(std::make_pair("", component_llcpp::kDefaultInstance),
                                         std::make_pair(".", component_llcpp::kDefaultInstance),
                                         std::make_pair("fuchsia.examples.EchoService", ""),
                                         std::make_pair("fuchsia.examples.EchoService", "")));

}  // namespace
