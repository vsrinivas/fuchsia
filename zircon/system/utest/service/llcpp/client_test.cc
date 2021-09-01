// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.service.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/service/llcpp/service.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace {

using Echo = ::fidl_service_test::Echo;
using EchoService = ::fidl_service_test::EchoService;

class EchoCommon : public fidl::WireServer<Echo> {
 public:
  explicit EchoCommon(const char* prefix) : prefix_(prefix) {}

  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
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
    auto default_member_foo = fbl::MakeRefCounted<fs::Service>([this](zx::channel request) {
      return default_foo_.Connect(dispatcher_, std::move(request));
    });
    auto default_member_bar = fbl::MakeRefCounted<fs::Service>([this](zx::channel request) {
      return default_bar_.Connect(dispatcher_, std::move(request));
    });
    auto default_instance = fbl::MakeRefCounted<fs::PseudoDir>();
    default_instance->AddEntry("foo", std::move(default_member_foo));
    default_instance->AddEntry("bar", std::move(default_member_bar));

    auto other_member_foo = fbl::MakeRefCounted<fs::Service>([this](zx::channel request) {
      return other_foo_.Connect(dispatcher_, std::move(request));
    });
    auto other_member_bar = fbl::MakeRefCounted<fs::Service>([this](zx::channel request) {
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
  zx::status<EchoService::ServiceClient> open_result = llcpp::sys::OpenServiceAt<EchoService>(svc_);
  ASSERT_TRUE(open_result.is_ok());

  EchoService::ServiceClient service = std::move(open_result.value());

  // Connect to the member 'foo'.
  zx::status<fidl::ClientEnd<Echo>> connect_result = service.connect_foo();
  ASSERT_TRUE(connect_result.is_ok());

  fidl::WireSyncClient<Echo> client = fidl::BindSyncClient(std::move(connect_result.value()));
  fidl::WireResult<Echo::EchoString> echo_result = client.EchoString(fidl::StringView("hello"));
  ASSERT_TRUE(echo_result.ok());

  auto response = echo_result.Unwrap();

  std::string result_string(response->response.data(), response->response.size());
  ASSERT_EQ(result_string, "default-foo: hello");
}

TEST_F(ClientTest, ConnectsToOther) {
  zx::status<EchoService::ServiceClient> open_result =
      llcpp::sys::OpenServiceAt<EchoService>(svc_, "other");
  ASSERT_TRUE(open_result.is_ok());

  EchoService::ServiceClient service = std::move(open_result.value());

  // Connect to the member 'bar'.
  zx::status<fidl::ClientEnd<Echo>> connect_result = service.connect_bar();
  ASSERT_TRUE(connect_result.is_ok());

  fidl::WireSyncClient<Echo> client = fidl::BindSyncClient(std::move(connect_result.value()));
  fidl::WireResult<Echo::EchoString> echo_result = client.EchoString(fidl::StringView("hello"));
  ASSERT_TRUE(echo_result.ok());

  auto response = echo_result.Unwrap();

  std::string result_string(response->response.data(), response->response.size());
  ASSERT_EQ(result_string, "other-bar: hello");
}

TEST_F(ClientTest, FilePathTooLong) {
  std::string illegal_path;
  illegal_path.assign(256, 'a');

  // Use an instance name that is too long.
  zx::status<EchoService::ServiceClient> open_result =
      llcpp::sys::OpenServiceAt<EchoService>(svc_, illegal_path);
  ASSERT_TRUE(open_result.is_error());
  ASSERT_EQ(open_result.status_value(), ZX_ERR_INVALID_ARGS);

  // Use a service name that is too long.
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_EQ(llcpp::sys::OpenNamedServiceAt(svc_, illegal_path, "default", std::move(remote))
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

//
// Tests for connecting to singleton FIDL services (`/svc/MyProtocolName` style).
//

struct MockProtocol {};

template <>
struct ::fidl::internal::ProtocolDetails<MockProtocol> {
  static constexpr char DiscoverableName[] = "mock";
};

// Test compile time path concatenation.
TEST(SingletonService, DefaultPath) {
  constexpr auto path = ::fidl::DiscoverableProtocolDefaultPath<MockProtocol>;
  ASSERT_STR_EQ(path, "/svc/mock", "protocol path should be /svc/mock");
}

// Using a local filesystem, test that |service::ConnectAt| successfully sends
// an open request using the path |fidl::DiscoverableProtocolName<MockProtocol>|,
// when connecting to the |MockProtocol| service.
TEST(SingletonService, ConnectAt) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fs::SynchronousVfs vfs(loop.dispatcher());

  // Set up the service directory with one fake protocol.
  sync_completion_t connected;
  auto protocol = fbl::MakeRefCounted<fs::Service>([&connected](zx::channel request) {
    sync_completion_signal(&connected);
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
  auto client_end = service::ConnectAt<MockProtocol>(directory->client);
  ASSERT_OK(client_end.status_value());

  ASSERT_OK(sync_completion_wait(&connected, zx::duration::infinite().get()));
  // Test that the request is dropped by the server.
  ASSERT_OK(client_end->channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));

  loop.Shutdown();
}

//
// Tests for cloning |fuchsia.io/Node|-like services.
//

TEST_F(ClientTest, CloneServiceDirectory) {
  auto svc_clone = service::Clone(svc_);
  ASSERT_OK(svc_clone.status_value());
  static_assert(
      std::is_same_v<decltype(svc_clone.value()), fidl::ClientEnd<fuchsia_io::Directory>&>);
  ASSERT_NE(svc_.channel(), svc_clone->channel().get());

  // Test that we can connect to services in the |svc_clone| directory.
  // Refer to |MockEchoService| for the directory layout.
  auto client_end = service::ConnectAt<Echo>(
      *svc_clone, (std::string(EchoService::Name) + "/default/foo").c_str());
  ASSERT_OK(client_end.status_value());
  auto echo = fidl::BindSyncClient(std::move(*client_end));
  auto result = echo.EchoString("foo");
  ASSERT_OK(result.status());
  ASSERT_STR_EQ(std::string(result->response.data(), result->response.size()).c_str(),
                "default-foo: foo");
}

TEST(CloneService, Error) {
  auto bad_endpoint = fidl::CreateEndpoints<fuchsia_io::Directory>();
  bad_endpoint->server.reset();

  auto failure = service::Clone(bad_endpoint->client);
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, failure.status_value());

  auto invalid = service::MaybeClone(bad_endpoint->client);
  ASSERT_FALSE(invalid.is_valid());
}
