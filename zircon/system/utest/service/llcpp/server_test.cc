// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/service/llcpp/outgoing_directory.h>
#include <lib/service/llcpp/service.h>
#include <lib/service/llcpp/service_handler.h>
#include <lib/zx/channel.h>

#include <iostream>

#include <fbl/unique_fd.h>
#include <fidl/service/test/llcpp/fidl.h>
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

  void EchoString(fidl::StringView input, EchoStringCompleter::Sync& completer) override {
    std::string reply = prefix_ + ": " + std::string(input.data(), input.size());
    completer.Reply(fidl::unowned_str(reply));
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
  ServerTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), outgoing_(loop_.dispatcher()) {}

  llcpp::sys::ServiceHandler SetUpInstance(Echo::Interface* foo_impl, Echo::Interface* bar_impl) {
    llcpp::sys::ServiceHandler handler;
    EchoService::Handler my_service(&handler);

    auto add_foo_result =
        my_service.add_foo([this, foo_impl](fidl::ServerEnd<Echo> request_channel) -> zx::status<> {
          // TODO(fxbug.dev/67062): |fidl::BindServer| should return a |zx::status|,
          // which would simplify this code.
          auto result = fidl::BindServer(loop_.dispatcher(), std::move(request_channel), foo_impl);
          if (result.is_ok())
            return zx::ok();
          return zx::make_status(result.take_error());
        });
    ZX_ASSERT(add_foo_result.is_ok());

    auto add_bar_result =
        my_service.add_bar([this, bar_impl](fidl::ServerEnd<Echo> request_channel) -> zx::status<> {
          // TODO(fxbug.dev/67062): |fidl::BindServer| should return a |zx::status|,
          // which would simplify this code.
          auto result = fidl::BindServer(loop_.dispatcher(), std::move(request_channel), bar_impl);
          if (result.is_ok())
            return zx::ok();
          return zx::make_status(result.take_error());
        });
    ZX_ASSERT(add_bar_result.is_ok());

    return handler;
  }

  void SetUp() override {
    loop_.StartThread("server-test-loop");

    outgoing_.AddService<EchoService>(SetUpInstance(&default_foo_, &default_bar_));
    outgoing_.AddService<EchoService>(SetUpInstance(&other_foo_, &other_bar_), "other");

    zx::channel remote;
    ASSERT_OK(zx::channel::create(0, &local_root_, &remote));

    outgoing_.Serve(std::move(remote));
  }

  void TearDown() override { loop_.Shutdown(); }

  EchoCommon default_foo_{"default-foo"};
  EchoCommon default_bar_{"default-bar"};
  EchoCommon other_foo_{"other-foo"};
  EchoCommon other_bar_{"other-bar"};

  async::Loop loop_;
  zx::channel local_root_;
  llcpp::sys::OutgoingDirectory outgoing_;
};

TEST_F(ServerTest, ConnectsToDefaultMember) {
  // Open a copy of the local namespace (channel) as a file descriptor.
  fbl::unique_fd svc_fd = OpenSvcDir(local_root_);
  ASSERT_TRUE(svc_fd.is_valid());

  // Extract the channel from `svc_fd`.
  zx_handle_t svc_local;
  ASSERT_OK(fdio_get_service_handle(svc_fd.release(), &svc_local));

  // Connect to the `EchoService` at the 'default' instance.
  zx::status<EchoService::ServiceClient> open_result =
      llcpp::sys::OpenServiceAt<EchoService>(zx::unowned_channel(svc_local));
  ASSERT_TRUE(open_result.is_ok());

  EchoService::ServiceClient service = std::move(open_result.value());

  // Connect to the member 'foo'.
  zx::status<fidl::ClientEnd<Echo>> connect_result = service.connect_foo();
  ASSERT_TRUE(connect_result.is_ok());

  Echo::SyncClient client = fidl::BindSyncClient(std::move(connect_result.value()));
  Echo::ResultOf::EchoString echo_result = client.EchoString(fidl::StringView("hello"));
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
  zx::status<EchoService::ServiceClient> open_result =
      llcpp::sys::OpenServiceAt<EchoService>(zx::unowned_channel(svc_local), "other");
  ASSERT_TRUE(open_result.is_ok());

  EchoService::ServiceClient service = std::move(open_result.value());

  // Connect to the member 'foo'.
  zx::status<fidl::ClientEnd<Echo>> connect_result = service.connect_foo();
  ASSERT_TRUE(connect_result.is_ok());

  Echo::SyncClient client = fidl::BindSyncClient(std::move(connect_result.value()));
  Echo::ResultOf::EchoString echo_result = client.EchoString(fidl::StringView("hello"));
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
  ASSERT_EQ(std::string(entry->d_name), "foo");

  entry = readdir(dir);
  ASSERT_NE(entry, nullptr);
  ASSERT_EQ(std::string(entry->d_name), "bar");

  ASSERT_EQ(readdir(dir), nullptr);
}
