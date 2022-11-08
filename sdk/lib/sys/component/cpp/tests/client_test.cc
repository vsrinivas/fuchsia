// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.service.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>

#include <latch>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace {

using Echo = fidl_service_test::Echo;
using EchoService = fidl_service_test::EchoService;

class EchoCommon : public fidl::WireServer<Echo> {
 public:
  explicit EchoCommon(const char* prefix) : prefix_(prefix) {}

  zx_status_t Connect(async_dispatcher_t* dispatcher, fidl::ServerEnd<Echo> request) {
    return fidl::BindSingleInFlightOnly(dispatcher, std::move(request), this);
  }

  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    std::string reply = prefix_ + ": " + std::string(request->value.data(), request->value.size());
    completer.Reply(fidl::StringView::FromExternal(reply));
  }

 private:
  std::string prefix_;
};

class MockEchoService {
 public:
  static constexpr const char* kName = EchoService::Name;

  // Build a VFS that looks like:
  //
  // fidl.service.test.EchoService/
  //                               default/
  //                                       foo (Echo)
  //                                       bar (Echo)
  //                               other/
  //                                       foo (Echo)
  //                                       bar (Echo)
  //
  explicit MockEchoService(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher), vfs_(dispatcher) {
    auto default_member_foo =
        fbl::MakeRefCounted<fs::Service>([this](fidl::ServerEnd<Echo> request) {
          return default_foo_.Connect(dispatcher_, std::move(request));
        });
    auto default_member_bar =
        fbl::MakeRefCounted<fs::Service>([this](fidl::ServerEnd<Echo> request) {
          return default_bar_.Connect(dispatcher_, std::move(request));
        });
    auto default_instance = fbl::MakeRefCounted<fs::PseudoDir>();
    default_instance->AddEntry("foo", std::move(default_member_foo));
    default_instance->AddEntry("bar", std::move(default_member_bar));

    auto other_member_foo = fbl::MakeRefCounted<fs::Service>([this](fidl::ServerEnd<Echo> request) {
      return other_foo_.Connect(dispatcher_, std::move(request));
    });
    auto other_member_bar = fbl::MakeRefCounted<fs::Service>([this](fidl::ServerEnd<Echo> request) {
      return other_bar_.Connect(dispatcher_, std::move(request));
    });
    auto other_instance = fbl::MakeRefCounted<fs::PseudoDir>();
    other_instance->AddEntry("foo", std::move(other_member_foo));
    other_instance->AddEntry("bar", std::move(other_member_bar));

    auto service = fbl::MakeRefCounted<fs::PseudoDir>();
    service->AddEntry("default", std::move(default_instance));
    service->AddEntry("other", std::move(other_instance));

    auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir->AddEntry(kName, std::move(service));

    auto svc_remote = fidl::CreateEndpoints(&svc_local_);
    ASSERT_OK(svc_remote.status_value());
    vfs_.ServeDirectory(root_dir, std::move(*svc_remote));
  }

  fidl::UnownedClientEnd<fuchsia_io::Directory> svc() { return svc_local_; }

 private:
  async_dispatcher_t* dispatcher_;
  fs::SynchronousVfs vfs_;
  EchoCommon default_foo_{"default-foo"};
  EchoCommon default_bar_{"default-bar"};
  EchoCommon other_foo_{"other-foo"};
  EchoCommon other_bar_{"other-bar"};
  fidl::ClientEnd<fuchsia_io::Directory> svc_local_;
};

}  // namespace

class ClientTest : public zxtest::Test {
 protected:
  ClientTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        echo_service_(loop_.dispatcher()),
        svc_(echo_service_.svc()) {}

  void SetUp() override { loop_.StartThread("client-test-loop"); }

  void TearDown() override { loop_.Shutdown(); }

  async::Loop loop_;
  MockEchoService echo_service_;
  fidl::UnownedClientEnd<fuchsia_io::Directory> svc_;
};

TEST_F(ClientTest, ConnectsToDefault) {
  zx::result<EchoService::ServiceClient> open_result = component::OpenServiceAt<EchoService>(svc_);
  ASSERT_TRUE(open_result.is_ok());

  EchoService::ServiceClient service = std::move(open_result.value());

  // Connect to the member 'foo'.
  zx::result<fidl::ClientEnd<Echo>> connect_result = service.connect_foo();
  ASSERT_TRUE(connect_result.is_ok());

  fidl::WireSyncClient client{std::move(connect_result.value())};
  fidl::WireResult<Echo::EchoString> echo_result = client->EchoString(fidl::StringView("hello"));
  ASSERT_TRUE(echo_result.ok());

  auto response = echo_result.Unwrap();

  std::string result_string(response->response.data(), response->response.size());
  ASSERT_EQ(result_string, "default-foo: hello");
}

TEST_F(ClientTest, ConnectsToDefaultExternalServerEnd) {
  zx::result<EchoService::ServiceClient> open_result = component::OpenServiceAt<EchoService>(svc_);
  ASSERT_TRUE(open_result.is_ok());

  EchoService::ServiceClient service = std::move(open_result.value());

  zx::result endpoints = fidl::CreateEndpoints<Echo>();
  ASSERT_OK(endpoints.status_value());

  // Connect to the member 'foo'.
  zx::result<> connect_result = service.connect_foo(std::move(endpoints->server));
  ASSERT_TRUE(connect_result.is_ok());

  fidl::WireSyncClient client{std::move(endpoints->client)};
  fidl::WireResult<Echo::EchoString> echo_result = client->EchoString(fidl::StringView("hello"));
  ASSERT_TRUE(echo_result.ok());

  auto response = echo_result.Unwrap();

  std::string result_string(response->response.data(), response->response.size());
  ASSERT_EQ(result_string, "default-foo: hello");
}

TEST_F(ClientTest, ConnectsToOther) {
  zx::result<EchoService::ServiceClient> open_result =
      component::OpenServiceAt<EchoService>(svc_, "other");
  ASSERT_TRUE(open_result.is_ok());

  EchoService::ServiceClient service = std::move(open_result.value());

  // Connect to the member 'bar'.
  zx::result<fidl::ClientEnd<Echo>> connect_result = service.connect_bar();
  ASSERT_TRUE(connect_result.is_ok());

  fidl::WireSyncClient client{std::move(connect_result.value())};
  fidl::WireResult<Echo::EchoString> echo_result = client->EchoString(fidl::StringView("hello"));
  ASSERT_TRUE(echo_result.ok());

  auto response = echo_result.Unwrap();

  std::string result_string(response->response.data(), response->response.size());
  ASSERT_EQ(result_string, "other-bar: hello");
}

TEST_F(ClientTest, FilePathTooLong) {
  std::string illegal_path;
  illegal_path.assign(256, 'a');

  // Use an instance name that is too long.
  zx::result<EchoService::ServiceClient> open_result =
      component::OpenServiceAt<EchoService>(svc_, illegal_path);
  ASSERT_TRUE(open_result.is_error());
  ASSERT_EQ(open_result.status_value(), ZX_ERR_INVALID_ARGS);

  // Use a service name that is too long.
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_EQ(component::OpenNamedServiceAt(svc_, illegal_path, "default", std::move(remote))
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

//
// Tests for connecting to singleton FIDL services (`/svc/MyProtocolName` style).
//

struct MockProtocol {
  using Transport = fidl::internal::ChannelTransport;
};

template <>
struct fidl::internal::ProtocolDetails<MockProtocol> {
  static constexpr char DiscoverableName[] = "mock";
  static constexpr bool kIsProtocol = true;
};

// Test compile time path concatenation.
TEST(SingletonService, DefaultPath) {
  constexpr auto path = fidl::DiscoverableProtocolDefaultPath<MockProtocol>;
  ASSERT_STREQ(path, "/svc/mock", "protocol path should be /svc/mock");
}

// Using a local filesystem, test that |component::ConnectAt| successfully sends
// an open request using the path |fidl::DiscoverableProtocolName<MockProtocol>|,
// when connecting to the |MockProtocol| service.
TEST(SingletonService, ConnectAt) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fs::SynchronousVfs vfs(loop.dispatcher());

  // Set up the service directory with one fake protocol.
  std::latch connected(2);
  auto protocol = fbl::MakeRefCounted<fs::Service>([&connected](zx::channel request) {
    connected.count_down();
    // Implicitly drop |request|.
    return ZX_OK;
  });
  auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  root_dir->AddEntry(fidl::DiscoverableProtocolName<MockProtocol>, std::move(protocol));

  auto directory = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(directory.status_value());
  ASSERT_OK(vfs.ServeDirectory(root_dir, std::move(directory->server)));
  loop.StartThread("SingletonService/ConnectAt");

  // Test connecting to that protocol.
  auto client_end = component::ConnectAt<MockProtocol>(directory->client);
  ASSERT_OK(client_end.status_value());

  auto endpoints = fidl::CreateEndpoints<MockProtocol>();
  ASSERT_OK(endpoints.status_value());
  {
    zx::result<> status = component::ConnectAt(directory->client, std::move(endpoints->server));
    ASSERT_OK(status.status_value());
  }

  connected.wait();

  // Test that the request is dropped by the server.
  ASSERT_OK(client_end->channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));
  ASSERT_OK(
      endpoints->client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));

  loop.Shutdown();
}

//
// Tests for cloning |fuchsia.io/Node|-like services.
//

TEST_F(ClientTest, CloneServiceDirectory) {
  auto svc_clone = component::Clone(svc_);
  ASSERT_OK(svc_clone.status_value());
  static_assert(
      std::is_same_v<decltype(svc_clone.value()), fidl::ClientEnd<fuchsia_io::Directory>&>);
  ASSERT_NE(svc_.channel(), svc_clone->channel().get());

  // Test that we can connect to services in the |svc_clone| directory.
  // Refer to |MockEchoService| for the directory layout.
  auto client_end = component::ConnectAt<Echo>(
      *svc_clone, (std::string(EchoService::Name) + "/default/foo").c_str());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient echo{std::move(*client_end)};
  auto result = echo->EchoString("foo");
  ASSERT_OK(result.status());
  ASSERT_STREQ(std::string(result.value().response.data(), result.value().response.size()).c_str(),
               "default-foo: foo");
}

TEST(CloneService, Error) {
  auto bad_endpoint = fidl::CreateEndpoints<fuchsia_io::Directory>();
  bad_endpoint->server.reset();

  auto failure = component::Clone(bad_endpoint->client);
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, failure.status_value());

  auto invalid = component::MaybeClone(bad_endpoint->client);
  ASSERT_FALSE(invalid.is_valid());
}
