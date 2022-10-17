// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.dispatcher.handle.ownership.test/cpp/wire.h>
#include <fidl/llcpptest.dispatcher.handle.ownership.test/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/fidl/cpp/wire/server.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

// These tests verify that the dispatchers properly close any unused handles
// ignored by the user method handlers.

namespace {

namespace test = ::llcpptest_dispatcher_handle_ownership_test;

// Creates a pair of |zx::eventpair|.
auto CreateEventPair() {
  struct {
    zx::eventpair e1;
    zx::eventpair e2;
  } ret;
  zx_status_t status = zx::eventpair::create(0, &ret.e1, &ret.e2);
  ZX_ASSERT(status == ZX_OK);
  return ret;
}

TEST(DispatcherHandleOwnership, ServerReceiveOneWay) {
  zx::result endpoints = fidl::CreateEndpoints<test::Protocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  class Server : public fidl::WireServer<test::Protocol> {
   public:
    void SendResource(SendResourceRequestView request,
                      SendResourceCompleter::Sync& completer) override {
      // The handles in |request| should be closed by the bindings runtime after we return.
    }

    void GetResource(GetResourceCompleter::Sync& completer) override {
      ZX_PANIC("Not used in test");
    }
  };
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::BindServer(loop.dispatcher(), std::move(remote), std::make_unique<Server>());
  fidl::WireClient client(std::move(local), loop.dispatcher());

  auto [observer, send] = CreateEventPair();
  fidl::Arena allocator;
  auto result = client->SendResource(
      test::wire::Resource::Builder(allocator).handle(std::move(send)).Build());
  ASSERT_TRUE(result.ok());

  ASSERT_OK(loop.RunUntilIdle());
  zx_signals_t signals;
  ASSERT_OK(observer.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), &signals));
  ASSERT_EQ(signals, ZX_EVENTPAIR_PEER_CLOSED);
}

TEST(DispatcherHandleOwnership, ClientReceiveTwoWay) {
  zx::result endpoints = fidl::CreateEndpoints<test::Protocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  class Server : public fidl::WireServer<test::Protocol> {
   public:
    void SendResource(SendResourceRequestView request,
                      SendResourceCompleter::Sync& completer) override {
      ZX_PANIC("Not used in test");
    }

    void GetResource(GetResourceCompleter::Sync& completer) override {
      auto [observer, send] = CreateEventPair();
      fidl::Arena allocator;
      observer_ = std::move(observer);
      completer.Reply(test::wire::Resource::Builder(allocator).handle(std::move(send)).Build());
    }

    const zx::eventpair& observer() const { return observer_; }

   private:
    zx::eventpair observer_;
  };
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto server = std::make_shared<Server>();
  fidl::BindServer(loop.dispatcher(), std::move(remote), server);
  fidl::WireClient client(std::move(local), loop.dispatcher());

  // Test the managed overload.
  {
    client->GetResource().ThenExactlyOnce(
        [&](fidl::WireUnownedResult<test::Protocol::GetResource>& result) {
          ASSERT_TRUE(result.ok());
          // The handles in |r| should be closed by the bindings runtime after we return.
        });

    ASSERT_OK(loop.RunUntilIdle());
    zx_signals_t signals;
    ASSERT_OK(
        server->observer().wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), &signals));
    ASSERT_EQ(signals, ZX_EVENTPAIR_PEER_CLOSED);
  }

  // Test the caller-allocating overload.
  {
    class ResponseContext final : public fidl::WireResponseContext<test::Protocol::GetResource> {
      void OnResult(fidl::WireUnownedResult<test::Protocol::GetResource>& r) override {
        // The handles in |r| should be closed by the bindings runtime after we return.
        ASSERT_OK(r.status());
      }
    };
    ResponseContext context;
    fidl::AsyncClientBuffer<test::Protocol::GetResource> buffer;
    client.buffer(buffer.view())->GetResource().ThenExactlyOnce(&context);

    ASSERT_OK(loop.RunUntilIdle());
    zx_signals_t signals;
    ASSERT_OK(
        server->observer().wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), &signals));
    ASSERT_EQ(signals, ZX_EVENTPAIR_PEER_CLOSED);
  }
}

TEST(DispatcherHandleOwnership, ClientReceiveEvent) {
  zx::result endpoints = fidl::CreateEndpoints<test::Protocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  class Server : public fidl::testing::WireTestBase<test::Protocol> {
   public:
    void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
      FAIL("%s not used by test", name.c_str());
    }
  };
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto server = std::make_shared<Server>();
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote), server);

  auto [observer, send] = CreateEventPair();
  fidl::Arena allocator;
  ASSERT_OK(fidl::WireSendEvent(server_binding)
                ->ResourceEvent(
                    test::wire::Resource::Builder(allocator).handle(std::move(send)).Build()));

  class EventHandler final : public fidl::WireAsyncEventHandler<test::Protocol> {
   public:
    void ResourceEvent(fidl::WireEvent<test::Protocol::ResourceEvent>* r) override {
      // The handles in |r| should be closed by the bindings runtime after we return.
    }
  };

  EventHandler event_handler;
  fidl::WireClient client(std::move(local), loop.dispatcher(), &event_handler);
  ASSERT_OK(loop.RunUntilIdle());
  zx_signals_t signals;
  ASSERT_OK(observer.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), &signals));
  ASSERT_EQ(signals, ZX_EVENTPAIR_PEER_CLOSED);
}

}  // namespace
