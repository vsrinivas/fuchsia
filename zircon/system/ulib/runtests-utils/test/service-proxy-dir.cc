// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include <fidl/test/echo/c/fidl.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <fs/vmo_file.h>
#include <runtests-utils/service-proxy-dir.h>
#include <zxtest/zxtest.h>

namespace fio = ::llcpp::fuchsia::io;

namespace {

class Echo {
 public:
  using EchoBinder = fidl::Binder<Echo>;

  Echo(std::string response) : response_(response) {}
  virtual ~Echo() {}

  virtual zx_status_t EchoString(const char*, size_t, fidl_txn_t* txn) {
    return fidl_test_echo_EchoEchoString_reply(txn, response_.c_str(), response_.size());
  }

  virtual zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) {
    static constexpr fidl_test_echo_Echo_ops_t kOps = {
        .EchoString = EchoBinder::BindMember<&Echo::EchoString>,
    };

    return EchoBinder::BindOps<fidl_test_echo_Echo_dispatch>(dispatcher, std::move(channel), this,
                                                             &kOps);
  }

 private:
  std::string response_;
};

constexpr char kTestString[] = "test";
constexpr char kEchoString[] = "echo";
constexpr char kProxyEchoString[] = "proxy_echo";

TEST(ServiceProxyDirTest, Simple) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<fs::SynchronousVfs> vfs;

  vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());

  Echo echo(kEchoString);
  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  dir->AddEntry(kEchoString, fbl::MakeRefCounted<fs::Service>(
                                 [&echo, dispatcher = loop.dispatcher()](zx::channel request) {
                                   return echo.Bind(dispatcher, std::move(request));
                                 }));
  ASSERT_OK(loop.StartThread());

  zx::channel dir_client, dir_server;
  ASSERT_OK(zx::channel::create(0, &dir_client, &dir_server));

  ASSERT_OK(vfs->ServeDirectory(std::move(dir), std::move(dir_server)));
  ASSERT_OK(loop.StartThread());

  Echo proxy_echo(kProxyEchoString);
  auto proxy_dir = fbl::MakeRefCounted<runtests::ServiceProxyDir>(std::move(dir_client));
  proxy_dir->AddEntry(kProxyEchoString,
                      fbl::MakeRefCounted<fs::Service>(
                          [&proxy_echo, dispatcher = loop.dispatcher()](zx::channel request) {
                            return proxy_echo.Bind(dispatcher, std::move(request));
                          }));
  ASSERT_OK(loop.StartThread());

  zx::channel proxy_dir_client, proxy_dir_server;
  ASSERT_OK(zx::channel::create(0, &proxy_dir_client, &proxy_dir_server));

  ASSERT_OK(vfs->ServeDirectory(std::move(proxy_dir), std::move(proxy_dir_server)));
  ASSERT_OK(loop.StartThread());

  // First check the service served directly by the proxy.
  {
    zx::channel h1, h2;
    ASSERT_OK(zx::channel::create(0, &h1, &h2));

    ASSERT_OK(fio::Directory::Call::Open(
                  zx::unowned_channel(proxy_dir_client),
                  fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_DESCRIBE,
                  0755, fidl::StringView(kProxyEchoString), std::move(h1))
                  .status());

    fio::Directory::EventHandlers handlers{
        .on_open = [&](fio::Directory::OnOpenResponse* message) -> zx_status_t {
          return message->s;
        },
        .unknown = []() -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
    };
    ASSERT_OK(fio::Directory::Call::HandleEvents(zx::unowned_channel(h2), handlers));

    char response_buffer[sizeof(kProxyEchoString)] = {};
    size_t response_size;
    ASSERT_OK(fidl_test_echo_EchoEchoString(h2.get(), kTestString, strlen(kTestString),
                                            response_buffer, sizeof(response_buffer),
                                            &response_size));
    ASSERT_EQ(strlen(kProxyEchoString), response_size);
    ASSERT_STR_EQ(kProxyEchoString, response_buffer);
  }

  // Second check the service that's being proxied by the proxy.
  {
    zx::channel h1, h2;
    ASSERT_OK(zx::channel::create(0, &h1, &h2));

    ASSERT_OK(fio::Directory::Call::Open(
                  zx::unowned_channel(proxy_dir_client),
                  fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_DESCRIBE,
                  0755, fidl::StringView(kEchoString), std::move(h1))
                  .status());
    fio::Directory::EventHandlers handlers{
        .on_open = [&](fio::Directory::OnOpenResponse* message) -> zx_status_t {
          return message->s;
        },
        .unknown = []() -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
    };
    ASSERT_OK(fio::Directory::Call::HandleEvents(zx::unowned_channel(h2), handlers));

    char response_buffer[sizeof(kEchoString)] = {};
    size_t response_size;
    ASSERT_OK(fidl_test_echo_EchoEchoString(h2.get(), kTestString, strlen(kTestString),
                                            response_buffer, sizeof(response_buffer),
                                            &response_size));
    ASSERT_EQ(strlen(kEchoString), response_size);
    ASSERT_STR_EQ(kEchoString, response_buffer);
  }

  loop.Shutdown();
}

}  // anonymous namespace
