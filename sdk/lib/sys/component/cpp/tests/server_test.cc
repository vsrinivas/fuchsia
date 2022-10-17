// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fidl.service.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/sys/component/cpp/handlers.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>

#include <iostream>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

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

fbl::unique_fd OpenRootDir(const zx::channel& request) {
  int root_fd = 0;
  zx_status_t result = fdio_fd_create(fdio_service_clone(request.get()), &root_fd);
  if (result != ZX_OK) {
    return {};
  }
  return fbl::unique_fd(root_fd);
}

fbl::unique_fd OpenSvcDir(const zx::channel& request) {
  fbl::unique_fd root_fd = OpenRootDir(request);
  if (!root_fd.is_valid()) {
    return {};
  }
  return fbl::unique_fd(openat(root_fd.get(), "svc", O_RDONLY));
}

fbl::unique_fd OpenAt(const fbl::unique_fd& dirfd, const char* path, int flags) {
  return fbl::unique_fd(openat(dirfd.get(), path, flags));
}

}  // namespace

class ServerTest : public zxtest::Test {
 protected:
  ServerTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        outgoing_(component::OutgoingDirectory::Create(loop_.dispatcher())) {}

  component::ServiceInstanceHandler SetUpInstance(fidl::WireServer<Echo>* foo_impl,
                                                  fidl::WireServer<Echo>* bar_impl) {
    component::ServiceInstanceHandler handler;
    EchoService::Handler my_service(&handler);

    auto add_foo_result =
        my_service.add_foo([this, foo_impl](fidl::ServerEnd<Echo> request_channel) -> void {
          fidl::BindServer(loop_.dispatcher(), std::move(request_channel), foo_impl);
        });
    ZX_ASSERT(add_foo_result.is_ok());

    auto add_bar_result =
        my_service.add_bar([this, bar_impl](fidl::ServerEnd<Echo> request_channel) -> void {
          fidl::BindServer(loop_.dispatcher(), std::move(request_channel), bar_impl);
        });
    ZX_ASSERT(add_bar_result.is_ok());

    return handler;
  }

  void SetUp() override {
    loop_.StartThread("server-test-loop");

    auto result = outgoing_.AddService<EchoService>(SetUpInstance(&default_foo_, &default_bar_));
    ASSERT_OK(result.status_value());
    result = outgoing_.AddService<EchoService>(SetUpInstance(&other_foo_, &other_bar_), "other");
    ASSERT_OK(result.status_value());

    zx::channel remote;
    ASSERT_OK(zx::channel::create(0, &local_root_, &remote));

    result = outgoing_.Serve(std::move(remote));
    ASSERT_OK(result.status_value());
  }

  void TearDown() override { loop_.Shutdown(); }

  EchoCommon default_foo_{"default-foo"};
  EchoCommon default_bar_{"default-bar"};
  EchoCommon other_foo_{"other-foo"};
  EchoCommon other_bar_{"other-bar"};

  async::Loop loop_;
  zx::channel local_root_;
  component::OutgoingDirectory outgoing_;
};

TEST_F(ServerTest, ConnectsToDefaultMember) {
  // Open a copy of the local namespace (channel) as a file descriptor.
  fbl::unique_fd svc_fd = OpenSvcDir(local_root_);
  ASSERT_TRUE(svc_fd.is_valid());

  // Extract the channel from `svc_fd`.
  zx_handle_t svc_local;
  ASSERT_OK(fdio_get_service_handle(svc_fd.release(), &svc_local));

  // Connect to the `EchoService` at the 'default' instance.
  zx::result<EchoService::ServiceClient> open_result =
      component::OpenServiceAt<EchoService>(zx::unowned_channel(svc_local));
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

TEST_F(ServerTest, ConnectsToOtherMember) {
  // Open a copy of the local namespace (channel) as a file descriptor.
  fbl::unique_fd svc_fd = OpenSvcDir(local_root_);
  ASSERT_TRUE(svc_fd.is_valid());

  // Extract the channel from `svc_fd`.
  zx_handle_t svc_local;
  ASSERT_OK(fdio_get_service_handle(svc_fd.release(), &svc_local));

  // Connect to the `EchoService` at the 'default' instance.
  zx::result<EchoService::ServiceClient> open_result =
      component::OpenServiceAt<EchoService>(zx::unowned_channel(svc_local), "other");
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
  ASSERT_EQ(result_string, "other-foo: hello");
}

TEST_F(ServerTest, ListsMembers) {
  // Open a copy of the local namespace (channel) as a file descriptor.
  fbl::unique_fd svc_fd = OpenSvcDir(local_root_);
  ASSERT_TRUE(svc_fd.is_valid());

  // Open the 'default' instance of the test service.
  fbl::unique_fd instance_fd = OpenAt(svc_fd, "fidl.service.test.EchoService/default", O_RDONLY);
  ASSERT_TRUE(instance_fd.is_valid());

  // fdopendir takes ownership of `instance_fd`.
  DIR* dir = fdopendir(instance_fd.release());
  ASSERT_NE(dir, nullptr);
  auto defer_closedir = fit::defer([dir] { closedir(dir); });

  dirent* entry = readdir(dir);
  ASSERT_NE(entry, nullptr);
  ASSERT_EQ(std::string(entry->d_name), ".");

  entry = readdir(dir);
  ASSERT_NE(entry, nullptr);
  ASSERT_EQ(std::string(entry->d_name), "bar");

  entry = readdir(dir);
  ASSERT_NE(entry, nullptr);
  ASSERT_EQ(std::string(entry->d_name), "foo");

  ASSERT_EQ(readdir(dir), nullptr);
}
