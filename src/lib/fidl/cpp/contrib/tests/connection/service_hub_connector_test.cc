// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl/cpp/contrib/connection/service_hub_connector.h"

#include <fidl/test.protocol.connector/cpp/fidl.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/sys/component/cpp/service_client.h>

#include <functional>
#include <optional>
#include <queue>
#include <type_traits>

#include <sdk/lib/sys/component/cpp/outgoing_directory.h>

#include "lib/async/cpp/task.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

constexpr size_t kMaxBufferSize = 20;

using test_protocol_connector::Error;
using test_protocol_connector::Protocol;
using test_protocol_connector::ProtocolFactory;

class ProtocolConnector : public fidl::contrib::ServiceHubConnector<ProtocolFactory, Protocol> {
 public:
  explicit ProtocolConnector(async_dispatcher_t* dispatcher,
                             fidl::UnownedClientEnd<fuchsia_io::Directory> directory)
      : ServiceHubConnector(dispatcher, kMaxBufferSize), directory_(directory) {}

  void DoAction() {
    Do([](fidl::Client<Protocol>& protocol, DoResolver resolver) {
      protocol->DoAction().Then(
          [resolver = std::move(resolver)](
              fidl::Result<test_protocol_connector::Protocol::DoAction>& status) mutable {
            resolver.resolve(status.is_error() &&
                             (status.error_value().is_framework_error() ||
                              status.error_value().domain_error() == Error::kTransient));
          });
    });
  }

 private:
  void ConnectToServiceHub(ServiceHubConnectResolver resolver) override {
    auto connection = component::ConnectAt<ProtocolFactory>(directory_);
    if (connection.is_error()) {
      resolver.resolve(std::nullopt);
    } else {
      resolver.resolve(std::move(connection.value()));
    }
  }

  void ConnectToService(fidl::Client<ProtocolFactory>& factory,
                        ServiceConnectResolver resolver) override {
    auto endpoints = fidl::CreateEndpoints<Protocol>();

    factory
        ->CreateProtocol(test_protocol_connector::ProtocolFactoryCreateProtocolRequest(
            std::move(endpoints->server)))
        .Then([resolver = std::move(resolver), client_end = std::move(endpoints->client)](
                  fidl::Result<ProtocolFactory::CreateProtocol>& response) mutable {
          if (response.is_ok()) {
            resolver.resolve(std::move(client_end));
          } else {
            resolver.resolve(std::nullopt);
          }
        });
  }

  fidl::UnownedClientEnd<fuchsia_io::Directory> directory_;
};

class ProtocolImpl : public fidl::Server<Protocol> {
 public:
  ProtocolImpl() = default;

  void DoAction(DoActionRequest& request, DoActionCompleter::Sync& completer) override {
    actions_attempted_ += 1;
    fit::result<Error> result = fit::ok();
    if (auto error = next_error()) {
      result = fit::as_error(*error);
    } else {
      actions_successful_ += 1;
    }

    completer.Reply(result);
  }

  size_t ActionsAttempted() const { return actions_attempted_; }
  size_t ActionsSuccessful() const { return actions_successful_; }
  void QueueError(Error error) { queued_errors_.push(error); }

 private:
  std::optional<Error> next_error() {
    if (queued_errors_.empty()) {
      return std::nullopt;
    }
    Error retval = queued_errors_.front();
    queued_errors_.pop();
    return retval;
  }

  size_t actions_attempted_ = 0;
  size_t actions_successful_ = 0;
  std::queue<Error> queued_errors_;
};

class FakeProtocolFactoryImpl : public fidl::Server<ProtocolFactory> {
 public:
  explicit FakeProtocolFactoryImpl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void CreateProtocol(CreateProtocolRequest& request,
                      CreateProtocolCompleter::Sync& completer) override {
    if (protocol_ == nullptr) {
      protocol_ = std::make_unique<ProtocolImpl>();
    }
    protocol_bindings_.push_back(
        fidl::BindServer(dispatcher_, std::move(request.protocol()), protocol_.get()));
    completer.Reply(fit::ok());
  }

  ProtocolImpl* protocol() { return protocol_.get(); }

  void DropAllProtocols() {
    std::vector<fidl::ServerBindingRef<Protocol>> bindings;
    protocol_bindings_.swap(bindings);

    for (auto binding : bindings) {
      FX_LOGS(INFO) << "Closing...";
      binding.Close(ZX_ERR_PEER_CLOSED);
    }
    protocol_ = nullptr;
  }

 private:
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<ProtocolImpl> protocol_;
  std::vector<fidl::ServerBindingRef<Protocol>> protocol_bindings_;
};

}  // namespace

class ServiceHubConnectorTest : public gtest::TestLoopFixture {
 public:
  ServiceHubConnectorTest() = default;
  ~ServiceHubConnectorTest() override = default;

  // Disallow copy and assign.
  ServiceHubConnectorTest(const ServiceHubConnectorTest&) = delete;
  ServiceHubConnectorTest& operator=(const ServiceHubConnectorTest&) = delete;
  ServiceHubConnectorTest(ServiceHubConnectorTest&&) = delete;
  ServiceHubConnectorTest& operator=(ServiceHubConnectorTest&&) = delete;

  FakeProtocolFactoryImpl& protocol_factory() { return *factory_impl_; }
  ProtocolImpl* protocol() { return factory_impl_->protocol(); }
  ProtocolConnector& protocol_connector() { return *protocol_connector_; }

  void ReplaceProtocol() {
    // Close all existing connections.
    if (!server_bindings_.empty()) {
      std::vector<fidl::ServerBindingRef<test_protocol_connector::ProtocolFactory>> old_bindings;
      old_bindings.swap(server_bindings_);

      for (auto binding : old_bindings) {
        binding.Close(ZX_ERR_PEER_CLOSED);
      }
      // Wait until all the closes happen.
      RunLoopUntilIdle();
    }

    // Create new factory impl.
    factory_impl_ = std::make_unique<FakeProtocolFactoryImpl>(dispatcher());
  }

  fidl::UnownedClientEnd<fuchsia_io::Directory> svc() const { return svc_dir_; }

  void DestroyProtocolConnector() { protocol_connector_ = nullptr; }

 private:
  void SetUp() override {
    ReplaceProtocol();

    // Serve ProtocolFactory
    outgoing_directory_ = std::make_unique<component::OutgoingDirectory>(
        component::OutgoingDirectory::Create(dispatcher()));
    ASSERT_EQ(ZX_OK,
              outgoing_directory_
                  ->AddProtocol<test_protocol_connector::ProtocolFactory>(
                      [this](fidl::ServerEnd<test_protocol_connector::ProtocolFactory> request) {
                        FX_LOGS(INFO) << "Binding attempted!";
                        server_bindings_.push_back(fidl::BindServer(
                            dispatcher(), std::move(request), factory_impl_.get()));
                      })
                  .status_value());

    // Connect to /svc endpoint
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_EQ(ZX_OK, outgoing_directory_->Serve(std::move(endpoints->server)).status_value());
    root_dir_ = std::move(endpoints->client);

    auto svc_dir = component::ConnectAt<fuchsia_io::Directory>(root_dir_, "svc");
    ASSERT_EQ(ZX_OK, svc_dir.status_value());
    svc_dir_ = std::move(svc_dir.value());

    // Build ProtocolConnector
    protocol_connector_ = std::make_unique<ProtocolConnector>(dispatcher(), svc());

    RunLoopUntilIdle();
  }

  std::unique_ptr<component::OutgoingDirectory> outgoing_directory_ = nullptr;
  fidl::ClientEnd<fuchsia_io::Directory> root_dir_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_dir_;
  std::unique_ptr<FakeProtocolFactoryImpl> factory_impl_;
  std::vector<fidl::ServerBindingRef<test_protocol_connector::ProtocolFactory>> server_bindings_;
  std::unique_ptr<ProtocolConnector> protocol_connector_;
};

TEST(ProtocolConnector, IsNotCopyOrMovable) {
  ASSERT_FALSE(std::is_copy_constructible_v<ProtocolConnector>);
  ASSERT_FALSE(std::is_copy_assignable_v<ProtocolConnector>);
  ASSERT_FALSE(std::is_move_constructible_v<ProtocolConnector>);
  ASSERT_FALSE(std::is_move_assignable_v<ProtocolConnector>);
}

TEST_F(ServiceHubConnectorTest, CallMethodAfterInitialization) {
  protocol_connector().DoAction();
  RunLoopFor(zx::min(100));
  ASSERT_NE(protocol(), nullptr);
  ASSERT_EQ(protocol()->ActionsSuccessful(), 1U);
}

TEST_F(ServiceHubConnectorTest, CallMethodBeforeInitialization) {
  constexpr size_t num_actions = 100u;
  ASSERT_GT(num_actions, kMaxBufferSize);

  ProtocolConnector protocol_connector(dispatcher(), svc());

  // Send multiple events before the connection is made.
  for (size_t i = 0; i < num_actions; i++) {
    protocol_connector.DoAction();
  }
  RunLoopUntilIdle();

  ASSERT_NE(protocol(), nullptr);
  ASSERT_EQ(protocol()->ActionsSuccessful(), kMaxBufferSize);

  // DO one more action to make sure the connector is in a good state.
  protocol_connector.DoAction();
  RunLoopUntilIdle();

  ASSERT_NE(protocol(), nullptr);
  ASSERT_EQ(protocol()->ActionsSuccessful(), kMaxBufferSize + 1);
}

TEST_F(ServiceHubConnectorTest, HandlesProtocolClose) {
  constexpr size_t num_actions = 10u;

  for (size_t i = 0; i < num_actions; i++) {
    protocol_connector().DoAction();
  }
  RunLoopUntilIdle();
  ASSERT_NE(protocol(), nullptr);
  ASSERT_EQ(protocol()->ActionsSuccessful(), num_actions);

  // Kill the protocol
  protocol_factory().DropAllProtocols();
  ASSERT_EQ(protocol(), nullptr);

  for (size_t i = 0; i < num_actions; i++) {
    protocol_connector().DoAction();
  }

  // RunLoop for 10 minutes to ensure that reconnect will be tried.
  RunLoopFor(zx::min(10));

  ASSERT_NE(protocol(), nullptr);
  ASSERT_EQ(protocol()->ActionsSuccessful(), num_actions);
}

TEST_F(ServiceHubConnectorTest, HandlesFactoryFailure) {
  constexpr size_t num_actions = 10u;

  for (size_t i = 0; i < num_actions; i++) {
    protocol_connector().DoAction();
  }
  RunLoopUntilIdle();
  ASSERT_NE(protocol(), nullptr);
  ASSERT_EQ(protocol()->ActionsSuccessful(), num_actions);

  // Kill the protocol factory
  ReplaceProtocol();
  ASSERT_EQ(protocol(), nullptr);

  for (size_t i = 0; i < num_actions; i++) {
    protocol_connector().DoAction();
  }

  // RunLoop for 10 minutes to ensure that reconnect will be tried.
  RunLoopFor(zx::min(10));

  ASSERT_NE(protocol(), nullptr);
  ASSERT_EQ(protocol()->ActionsSuccessful(), num_actions);
}

TEST_F(ServiceHubConnectorTest, RetriesTransientErrors) {
  protocol_connector().DoAction();
  RunLoopUntilIdle();
  ASSERT_NE(protocol(), nullptr);
  ASSERT_EQ(protocol()->ActionsSuccessful(), 1U);

  protocol()->QueueError(test_protocol_connector::Error::kTransient);
  protocol()->QueueError(test_protocol_connector::Error::kTransient);
  protocol_connector().DoAction();
  RunLoopFor(zx::min(10));

  // DoAction should have been called 4 times = 2 successes, 2 transient failures
  ASSERT_EQ(protocol()->ActionsAttempted(), 4U);
  ASSERT_EQ(protocol()->ActionsSuccessful(), 2U);
}

TEST_F(ServiceHubConnectorTest, DoesNotRetryPermanentErrors) {
  protocol_connector().DoAction();
  RunLoopUntilIdle();
  ASSERT_NE(protocol(), nullptr);
  ASSERT_EQ(protocol()->ActionsSuccessful(), 1U);

  protocol()->QueueError(test_protocol_connector::Error::kPermanent);
  protocol()->QueueError(test_protocol_connector::Error::kPermanent);

  // First permanent failure. Should be attempted once, but not succeed.
  protocol_connector().DoAction();
  RunLoopFor(zx::hour(1));
  ASSERT_EQ(protocol()->ActionsAttempted(), 2U);
  ASSERT_EQ(protocol()->ActionsSuccessful(), 1U);

  // Second permanent failure. Should be attempted once, but not succeed.
  protocol_connector().DoAction();
  RunLoopFor(zx::hour(1));
  ASSERT_EQ(protocol()->ActionsAttempted(), 3U);
  ASSERT_EQ(protocol()->ActionsSuccessful(), 1U);

  // Third attempt is successful. Should increment both attempted and successful.
  protocol_connector().DoAction();
  RunLoopFor(zx::hour(1));
  ASSERT_EQ(protocol()->ActionsAttempted(), 4U);
  ASSERT_EQ(protocol()->ActionsSuccessful(), 2U);
}

TEST_F(ServiceHubConnectorTest, SupportCallsFromDispatcherThread) {
  protocol_connector().DoAction();
  RunLoopUntilIdle();
}

TEST_F(ServiceHubConnectorTest, LimitsInFlightCallbacks) {
  // Store the DoResolvers in a vector to hold the do callback as 'in flight'.
  std::vector<ProtocolConnector::DoResolver> held_resolvers;

  for (size_t i = 0; i < kMaxBufferSize * 2; i++) {
    protocol_connector().Do(
        [&](fidl::Client<Protocol>& protocol, ProtocolConnector::DoResolver resolver) mutable {
          held_resolvers.push_back(std::move(resolver));
        });
    RunLoopUntilIdle();

    // We should see a DoResolver stored for each call until we reach kMaxBufferSize
    if (i < kMaxBufferSize) {
      EXPECT_EQ(held_resolvers.size(), i + 1);
    } else {
      EXPECT_EQ(held_resolvers.size(), kMaxBufferSize);
    }
  }
  ASSERT_EQ(held_resolvers.size(), kMaxBufferSize);
}
