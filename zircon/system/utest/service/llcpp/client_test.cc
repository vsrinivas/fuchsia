// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/channel.h>

#include <fidl/service/test/llcpp/fidl.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <zxtest/zxtest.h>

namespace {

using Echo = ::llcpp::fidl::service::test::Echo;
using EchoService = ::llcpp::fidl::service::test::EchoService;

class EchoCommon : public Echo::Interface {
 public:
  explicit EchoCommon(const char* prefix) : prefix_(prefix) {}

  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
    return fidl::BindSingleInFlightOnly(dispatcher, std::move(request), this);
  }

  void EchoString(fidl::StringView input, EchoStringCompleter::Sync completer) override {
    std::string reply = prefix_ + ": " + std::string(input.data(), input.size());
    completer.Reply(fidl::unowned_str(reply));
  }

 private:
  std::string prefix_;
};

class MockEchoService {
 public:
  static constexpr const char* kName = EchoService::Name;

  // Build a VFS that looks like:
  //
  // fuchsia.echo.EchoService/
  //                          default/
  //                                  foo (Echo)
  //                                  bar (Echo)
  //                          other/
  //                                  foo (Echo)
  //                                  bar (Echo)
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

    zx::channel svc_remote;
    ASSERT_OK(zx::channel::create(0, &svc_local_, &svc_remote));
    vfs_.ServeDirectory(root_dir, std::move(svc_remote));
  }

  zx::unowned_channel svc() { return zx::unowned_channel(svc_local_); }

 private:
  async_dispatcher_t* dispatcher_;
  fs::SynchronousVfs vfs_;
  EchoCommon default_foo_{"default-foo"};
  EchoCommon default_bar_{"default-bar"};
  EchoCommon other_foo_{"other-foo"};
  EchoCommon other_bar_{"other-bar"};
  zx::channel svc_local_;
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
  zx::unowned_channel svc_;
};

TEST_F(ClientTest, ConnectsToDefault) {
  fidl::result<EchoService::ServiceClient> open_result =
      llcpp::sys::OpenServiceAt<EchoService>(std::move(svc_));
  ASSERT_TRUE(open_result.is_ok());

  EchoService::ServiceClient service = open_result.take_value();

  // Connect to the member 'foo'.
  fidl::result<fidl::ClientChannel<Echo>> connect_result = service.connect_foo();
  ASSERT_TRUE(connect_result.is_ok());

  Echo::SyncClient client = fidl::BindSyncClient(connect_result.take_value());
  Echo::ResultOf::EchoString echo_result = client.EchoString(fidl::StringView("hello"));
  ASSERT_TRUE(echo_result.ok());

  auto response = echo_result.Unwrap();

  std::string result_string(response->response.data(), response->response.size());
  ASSERT_EQ(result_string, "default-foo: hello");
}

TEST_F(ClientTest, ConnectsToOther) {
  fidl::result<EchoService::ServiceClient> open_result =
      llcpp::sys::OpenServiceAt<EchoService>(std::move(svc_), "other");
  ASSERT_TRUE(open_result.is_ok());

  EchoService::ServiceClient service = open_result.take_value();

  // Connect to the member 'bar'.
  fidl::result<fidl::ClientChannel<Echo>> connect_result = service.connect_bar();
  ASSERT_TRUE(connect_result.is_ok());

  Echo::SyncClient client = fidl::BindSyncClient(connect_result.take_value());
  Echo::ResultOf::EchoString echo_result = client.EchoString(fidl::StringView("hello"));
  ASSERT_TRUE(echo_result.ok());

  auto response = echo_result.Unwrap();

  std::string result_string(response->response.data(), response->response.size());
  ASSERT_EQ(result_string, "other-bar: hello");
}

TEST_F(ClientTest, FilePathTooLong) {
  std::string illegal_path;
  illegal_path.assign(256, 'a');

  // Use an instance name that is too long.
  zx::unowned_channel svc_copy(*svc_);
  fidl::result<EchoService::ServiceClient> open_result =
      llcpp::sys::OpenServiceAt<EchoService>(std::move(svc_copy), illegal_path);
  ASSERT_TRUE(open_result.is_error());
  ASSERT_EQ(open_result.error(), ZX_ERR_INVALID_ARGS);

  // Use a service name that is too long.
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_EQ(
      llcpp::sys::OpenNamedServiceAt(std::move(svc_), illegal_path, "default", std::move(remote)),
      ZX_ERR_INVALID_ARGS);
}
