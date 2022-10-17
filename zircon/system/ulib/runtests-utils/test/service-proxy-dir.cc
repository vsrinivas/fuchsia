// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.echo/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include <runtests-utils/service-proxy-dir.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/storage/vfs/cpp/vmo_file.h"

namespace fio = fuchsia_io;

namespace {

class Echo : public fidl::WireServer<fidl_test_echo::Echo> {
 public:
  explicit Echo(std::string response) : response_(std::move(response)) {}

  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    completer.Reply(fidl::StringView::FromExternal(response_));
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
  dir->AddEntry(
      kEchoString,
      fbl::MakeRefCounted<fs::Service>(
          [&echo, dispatcher = loop.dispatcher()](fidl::ServerEnd<fidl_test_echo::Echo> request) {
            fidl::BindServer(dispatcher, std::move(request), &echo);
            return ZX_OK;
          }));
  ASSERT_OK(loop.StartThread());

  zx::result endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(endpoints.status_value());

  ASSERT_OK(vfs->ServeDirectory(std::move(dir), std::move(endpoints->server)));
  ASSERT_OK(loop.StartThread());

  Echo proxy_echo(kProxyEchoString);
  auto proxy_dir = fbl::MakeRefCounted<runtests::ServiceProxyDir>(std::move(endpoints->client));
  proxy_dir->AddEntry(
      kProxyEchoString,
      fbl::MakeRefCounted<fs::Service>([&proxy_echo, dispatcher = loop.dispatcher()](
                                           fidl::ServerEnd<fidl_test_echo::Echo> request) {
        fidl::BindServer(dispatcher, std::move(request), &proxy_echo);
        return ZX_OK;
      }));
  ASSERT_OK(loop.StartThread());

  zx::result proxy_endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(proxy_endpoints.status_value());

  ASSERT_OK(vfs->ServeDirectory(std::move(proxy_dir), std::move(proxy_endpoints->server)));
  ASSERT_OK(loop.StartThread());

  // First check the service served directly by the proxy.
  {
    zx::result endpoints = fidl::CreateEndpoints<fio::Node>();
    ASSERT_OK(endpoints.status_value());

    ASSERT_OK(fidl::WireCall(proxy_endpoints->client)
                  ->Open(fio::wire::OpenFlags::kRightReadable |
                             fio::wire::OpenFlags::kRightWritable | fio::wire::OpenFlags::kDescribe,
                         0755, fidl::StringView(kProxyEchoString), std::move(endpoints->server))
                  .status());

    class EventHandler : public fidl::WireSyncEventHandler<fio::Node> {
     public:
      EventHandler() = default;

      zx_status_t status() const { return status_; }

      void OnOpen(fidl::WireEvent<fio::Node::OnOpen>* event) override { status_ = event->s; }

      void OnRepresentation(fidl::WireEvent<fio::Node::OnRepresentation>* event) override {
        ADD_FAILURE("OnRepresentation is not supported");
      }

     private:
      zx_status_t status_ = ZX_ERR_NOT_SUPPORTED;
    };

    EventHandler event_handler;
    ASSERT_OK(event_handler.HandleOneEvent(endpoints->client));
    ASSERT_OK(event_handler.status());

    const fidl::WireResult result = fidl::WireCall(fidl::UnownedClientEnd<fidl_test_echo::Echo>{
                                                       endpoints->client.channel().borrow()})
                                        ->EchoString(kTestString);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_EQ(response.response.get(), kProxyEchoString);
  }

  // Second check the service that's being proxied by the proxy.
  {
    zx::result endpoints = fidl::CreateEndpoints<fio::Node>();
    ASSERT_OK(endpoints.status_value());

    ASSERT_OK(fidl::WireCall(proxy_endpoints->client)
                  ->Open(fio::wire::OpenFlags::kRightReadable |
                             fio::wire::OpenFlags::kRightWritable | fio::wire::OpenFlags::kDescribe,
                         0755, fidl::StringView(kEchoString), std::move(endpoints->server))
                  .status());

    class EventHandler : public fidl::WireSyncEventHandler<fio::Node> {
     public:
      EventHandler() = default;

      zx_status_t status() const { return status_; }

      void OnOpen(fidl::WireEvent<fio::Node::OnOpen>* event) override { status_ = event->s; }

      void OnRepresentation(fidl::WireEvent<fio::Node::OnRepresentation>* event) override {
        ADD_FAILURE("OnRepresentation is not supported");
      }

     private:
      zx_status_t status_ = ZX_ERR_NOT_SUPPORTED;
    };

    EventHandler event_handler;
    ASSERT_OK(event_handler.HandleOneEvent(endpoints->client));
    ASSERT_OK(event_handler.status());

    const fidl::WireResult result = fidl::WireCall(fidl::UnownedClientEnd<fidl_test_echo::Echo>{
                                                       endpoints->client.channel().borrow()})
                                        ->EchoString(kTestString);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_EQ(response.response.get(), kEchoString);
  }

  loop.Shutdown();
}

}  // anonymous namespace
