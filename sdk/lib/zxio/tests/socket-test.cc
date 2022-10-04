// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fidl/fuchsia.posix.socket.packet/cpp/wire_test_base.h>
#include <fidl/fuchsia.posix.socket.raw/cpp/wire_test_base.h>
#include <fidl/fuchsia.posix.socket/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/socket.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <lib/zxio/cpp/socket_address.h>
#include <lib/zxio/zxio.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/private.h"
#include "sdk/lib/zxio/tests/test_socket_server.h"

namespace fnet = fuchsia_net;
namespace fposix = fuchsia_posix;
namespace fsocket = fuchsia_posix_socket;
namespace fsocket_packet = fuchsia_posix_socket_packet;
namespace fsocket_raw = fuchsia_posix_socket_raw;

namespace std {
ostream& operator<<(ostream& os, const ErrOrOutCode& error) {
  return os << (error.is_error() ? error.status_string() : strerror(error.value()));
}
}  // namespace std

namespace {

class SynchronousDatagramSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::eventpair::create(0u, &event0_, &event1_));

    zx::status node_server = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(node_server.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*node_server), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_synchronous_datagram_socket_init(&storage_, TakeEvent(), TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  zx::eventpair TakeEvent() { return std::move(event0_); }
  fidl::ClientEnd<fsocket::SynchronousDatagramSocket> TakeClientEnd() {
    return std::move(client_end_);
  }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx::eventpair event0_, event1_;
  fidl::ClientEnd<fsocket::SynchronousDatagramSocket> client_end_;
  zxio_tests::SynchronousDatagramSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

TEST_F(SynchronousDatagramSocketTest, Basic) { Init(); }

TEST_F(SynchronousDatagramSocketTest, Release) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(SynchronousDatagramSocketTest, Borrow) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

TEST_F(SynchronousDatagramSocketTest, CreateWithType) {
  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_SYNCHRONOUS_DATAGRAM_SOCKET,
                                  TakeEvent().release(), TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}

class StreamSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_, &peer_));
    ASSERT_OK(socket_.get_info(ZX_INFO_SOCKET, &info_, sizeof(info_), nullptr, nullptr));

    zx::status server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(server_end.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*server_end), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_stream_socket_init(&storage_, TakeSocket(), info(), /*is_connected=*/false,
                                      TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  zx_info_socket_t& info() { return info_; }
  zx::socket TakeSocket() { return std::move(socket_); }
  fidl::ClientEnd<fsocket::StreamSocket> TakeClientEnd() { return std::move(client_end_); }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx_info_socket_t info_;
  zx::socket socket_, peer_;
  fidl::ClientEnd<fsocket::StreamSocket> client_end_;
  zxio_tests::StreamSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

TEST_F(StreamSocketTest, Basic) { Init(); }

TEST_F(StreamSocketTest, Release) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(StreamSocketTest, Borrow) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

TEST_F(StreamSocketTest, CreateWithType) {
  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_STREAM_SOCKET, TakeSocket().release(),
                                  &info(), /*is_connected=*/false,
                                  TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}

class DatagramSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &socket_, &peer_));
    ASSERT_OK(socket_.get_info(ZX_INFO_SOCKET, &info_, sizeof(info_), nullptr, nullptr));

    zx::status server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(server_end.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*server_end), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_datagram_socket_init(&storage_, TakeSocket(), info(), prelude_size(),
                                        TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  const zx_info_socket_t& info() const { return info_; }
  const zxio_datagram_prelude_size_t& prelude_size() const { return prelude_size_; }
  zx::socket TakeSocket() { return std::move(socket_); }
  fidl::ClientEnd<fsocket::DatagramSocket> TakeClientEnd() { return std::move(client_end_); }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx_info_socket_t info_;
  const zxio_datagram_prelude_size_t prelude_size_{};
  zx::socket socket_, peer_;
  fidl::ClientEnd<fsocket::DatagramSocket> client_end_;
  zxio_tests::DatagramSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

TEST_F(DatagramSocketTest, Basic) { Init(); }

TEST_F(DatagramSocketTest, Release) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(DatagramSocketTest, Borrow) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

TEST_F(DatagramSocketTest, CreateWithType) {
  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET,
                                  TakeSocket().release(), &info(), &prelude_size(),
                                  TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}

class DatagramSocketServer final : public fidl::testing::WireTestBase<fsocket::DatagramSocket> {
 public:
  [[nodiscard]] bool TakeGetErrorCalled() { return get_error_called_.exchange(false); }

  [[nodiscard]] bool TakeSendMsgPreflightCalled() {
    return send_msg_preflight_called_.exchange(false);
  }

  void InvalidateClientCache() {
    std::lock_guard lock(lock_);
    ASSERT_OK(zx::eventpair::create(0u, &cache_local_, &cache_peer_));
  }

  static constexpr fposix::wire::Errno kSocketError = fposix::wire::Errno::kEio;

  void GetError(GetErrorCompleter::Sync& completer) override {
    bool previously_called = get_error_called_.exchange(true);
    EXPECT_FALSE(previously_called) << "GetError was called but unacknowledged by the test";
    completer.ReplyError(kSocketError);
  }

  static constexpr size_t kMaximumSize = 1337;

  void SendMsgPreflight(fsocket::wire::DatagramSocketSendMsgPreflightRequest* request,
                        SendMsgPreflightCompleter::Sync& completer) override {
    bool previously_called = send_msg_preflight_called_.exchange(true);
    EXPECT_FALSE(previously_called) << "SendMsgPreflight was called but unacknowledged by the test";
    fidl::Arena alloc;
    fidl::WireTableBuilder response_builder =
        fsocket::wire::DatagramSocketSendMsgPreflightResponse::Builder(alloc);
    if (request->has_to()) {
      response_builder.to(request->to());
    } else {
      response_builder.to(ConnectedAddr(alloc));
    }
    zx::status<zx::eventpair> event = DuplicateCachePeer();
    ASSERT_OK(event.status_value()) << "failed to duplicate peer event for cache invalidation";
    std::array validity{std::move(event.value())};
    response_builder.validity(fidl::VectorView<zx::eventpair>::FromExternal(validity))
        .maximum_size(kMaximumSize);
    completer.ReplySuccess(response_builder.Build());
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE() << "unexpected message received: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  fnet::wire::SocketAddress ConnectedAddr(fidl::AnyArena& alloc) {
    static constexpr fidl::Array<uint8_t, 4> kConnectedAddr = {192, 0, 2, 99};
    static constexpr uint16_t kConnectedPort = 45678;
    return fnet::wire::SocketAddress::WithIpv4(alloc, fnet::wire::Ipv4SocketAddress{
                                                          .address =
                                                              {
                                                                  .addr = kConnectedAddr,
                                                              },
                                                          .port = kConnectedPort,
                                                      });
  }

  zx::status<zx::eventpair> DuplicateCachePeer() {
    std::lock_guard lock(lock_);
    zx::eventpair dup;
    if (zx_status_t status = cache_peer_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup); status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(std::move(dup));
  }

  std::atomic<bool> get_error_called_;
  std::atomic<bool> send_msg_preflight_called_;

  std::mutex lock_;
  zx::eventpair cache_local_ __TA_GUARDED(lock_);
  zx::eventpair cache_peer_ __TA_GUARDED(lock_);
};

class DatagramSocketRouteCacheTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::eventpair::create(0u, &error_local_, &error_peer_));
    ASSERT_NO_FATAL_FAILURE(server_.InvalidateClientCache());

    zx::status endpoints = fidl::CreateEndpoints<fsocket::DatagramSocket>();
    ASSERT_OK(endpoints.status_value());
    client_ = fidl::WireSyncClient<fsocket::DatagramSocket>{std::move(endpoints->client)};

    fidl::BindServer(control_loop_.dispatcher(), std::move(endpoints->server), &server_);
    control_loop_.StartThread("control");
  }

  void TearDown() final { control_loop_.Shutdown(); }

  void MakeSockAddrV4(uint16_t port, std::optional<SocketAddress>& out_addr) {
    constexpr char kSomeIpv4Addr[] = "192.0.2.55";
    struct sockaddr_in sockaddr;
    sockaddr.sin_family = AF_INET;
    ASSERT_EQ(inet_pton(AF_INET, kSomeIpv4Addr, &sockaddr.sin_addr), 1,
              "failed to create IPv4 sockaddr from addr '%s' and port '%d'", kSomeIpv4Addr, port);
    sockaddr.sin_port = htons(port);
    SocketAddress addr;
    addr.LoadSockAddr(reinterpret_cast<struct sockaddr*>(&sockaddr), sizeof(sockaddr));
    out_addr.emplace(addr);
  }

  void MakeSockAddrV6(uint16_t port, std::optional<SocketAddress>& out_addr) {
    constexpr char kSomeIpv6Addr[] = "2001:db8::55";
    struct sockaddr_in6 sockaddr;
    sockaddr.sin6_family = AF_INET6;
    ASSERT_EQ(inet_pton(AF_INET6, kSomeIpv6Addr, &sockaddr.sin6_addr), 1,
              "failed to create IPv6 sockaddr from addr '%s' and port '%d'", kSomeIpv6Addr, port);
    sockaddr.sin6_port = htons(port);
    SocketAddress addr;
    addr.LoadSockAddr(reinterpret_cast<struct sockaddr*>(&sockaddr), sizeof(sockaddr));
    out_addr.emplace(addr);
  }

  void GetFromCacheAssertSuccess(std::optional<SocketAddress>& addr) {
    zx_wait_item_t error_wait_item{
        .handle = error_peer_.get(),
        .waitfor = kSignalError,
    };
    RouteCache::Result result = cache_.Get(addr, std::nullopt, error_wait_item, client_);

    ASSERT_TRUE(result.is_ok()) << "RouteCache::Get failed: " << result.error_value();
    ASSERT_EQ(result.value(), DatagramSocketServer::kMaximumSize);
  }

 protected:
  static constexpr zx_signals_t kSignalError = ZX_USER_SIGNAL_0;
  DatagramSocketServer server_;
  RouteCache cache_;
  fidl::WireSyncClient<fsocket::DatagramSocket> client_;
  zx::eventpair error_local_, error_peer_;

 private:
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

constexpr uint16_t kSomePort = 10000;

TEST_F(DatagramSocketRouteCacheTest, GetNewItemCallsPreflight) {
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());

  std::optional<SocketAddress> to;
  ASSERT_NO_FATAL_FAILURE(MakeSockAddrV4(kSomePort, to));
  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());
}

TEST_F(DatagramSocketRouteCacheTest, GetExistingItemDoesntCallPreflight) {
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());

  std::optional<SocketAddress> to;
  ASSERT_NO_FATAL_FAILURE(MakeSockAddrV4(kSomePort, to));
  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());

  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());
}

TEST_F(DatagramSocketRouteCacheTest, InvalidateClientCacheGetCallsPreflight) {
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());

  std::optional<SocketAddress> to;
  ASSERT_NO_FATAL_FAILURE(MakeSockAddrV6(kSomePort, to));
  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());

  // When the server-side eventpair is closed for an existing item in the cache,
  // the client should observe the cache invalidation and call SendMsgPreflight
  // again the next time the item is retrieved from the cache.
  ASSERT_NO_FATAL_FAILURE(server_.InvalidateClientCache());

  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());
}

TEST_F(DatagramSocketRouteCacheTest, ErrorSignaledGetCallsGetError) {
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());

  // When the designated error signal is signaled on the error wait item, the
  // client should call `GetError` and propagate the error it receives to the
  // caller.
  ASSERT_OK(error_local_.signal_peer(0, kSignalError));

  std::optional<SocketAddress> to;
  ASSERT_NO_FATAL_FAILURE(MakeSockAddrV6(kSomePort, to));
  zx_wait_item_t error_wait_item{
      .handle = error_peer_.get(),
      .waitfor = kSignalError,
  };
  RouteCache::Result result = cache_.Get(to, std::nullopt, error_wait_item, client_);

  ASSERT_TRUE(result.is_error());
  ErrOrOutCode err_value = result.error_value();
  ASSERT_OK(err_value.status_value()) << "RouteCache::Get returned an error instead of an out code";
  ASSERT_EQ(err_value.value(), static_cast<int16_t>(DatagramSocketServer::kSocketError));
  ASSERT_TRUE(server_.TakeGetErrorCalled());
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());
}

TEST_F(DatagramSocketRouteCacheTest, ErrorPropagatedEvenIfCacheAlsoInvalidated) {
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());

  std::optional<SocketAddress> to;
  ASSERT_NO_FATAL_FAILURE(MakeSockAddrV6(kSomePort, to));
  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());

  // Close the server-side eventpair, *and* signal an error on the error wait
  // item. The error should take precedence and be returned to the caller
  // without the client calling `SendMsgPreflight`.
  ASSERT_NO_FATAL_FAILURE(server_.InvalidateClientCache());
  ASSERT_OK(error_local_.signal_peer(0, kSignalError));

  zx_wait_item_t error_wait_item{
      .handle = error_peer_.get(),
      .waitfor = kSignalError,
  };
  RouteCache::Result result = cache_.Get(to, std::nullopt, error_wait_item, client_);

  ASSERT_TRUE(result.is_error());
  ErrOrOutCode err_value = result.error_value();
  ASSERT_OK(err_value.status_value()) << "RouteCache::Get returned an error instead of an out code";
  ASSERT_EQ(err_value.value(), static_cast<int16_t>(DatagramSocketServer::kSocketError));
  ASSERT_TRUE(server_.TakeGetErrorCalled());
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());
}

TEST_F(DatagramSocketRouteCacheTest, LruDiscardedGetCallsPreflight) {
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());
  constexpr uint16_t kEphemeralPortStart = 32768;

  // For each new address we `Get`, the client should call `SendMsgPreflight`
  // since the address is not yet present in the cache.
  std::array<std::optional<SocketAddress>, RouteCache::kMaxEntries> addrs;
  for (size_t i = 0; i < addrs.size(); i++) {
    addrs[i] = std::optional<SocketAddress>{};
    ASSERT_NO_FATAL_FAILURE(
        MakeSockAddrV4(static_cast<uint16_t>(kEphemeralPortStart + i), addrs[i]));
    ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(addrs[i]),
                            "RouteCache::Get failed on addr %zu", i);
    ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());
  }

  // Once the addresses are in the cache, even though the cache is full, `Get`
  // should not require a call to `SendMsgPreflight`.
  for (size_t i = 0; i < addrs.size(); i++) {
    ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(addrs[i]),
                            "RouteCache::Get failed on addr %zu", i);
    ASSERT_FALSE(
        server_.TakeSendMsgPreflightCalled(),
        "RouteCache::Get should not call SendMsgPreflight for a cached address; did for addr %zu",
        i);
  }

  // Adding a new address causes the cache to go over capacity, and the least-
  // recently-used entry will be evicted, thus requiring a call to
  // `SendMsgPreflight` the next time it's queried.
  std::optional<SocketAddress> to;
  ASSERT_NO_FATAL_FAILURE(
      MakeSockAddrV4(static_cast<uint16_t>(kEphemeralPortStart + RouteCache::kMaxEntries), to));
  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());

  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(addrs[0]));
  ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());
}

TEST_F(DatagramSocketRouteCacheTest, SameAddressDifferentPortIsDifferentItem) {
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());

  std::optional<SocketAddress> to;
  ASSERT_NO_FATAL_FAILURE(MakeSockAddrV4(kSomePort, to));
  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());

  ASSERT_NO_FATAL_FAILURE(MakeSockAddrV4(kSomePort + 1, to));
  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());
}

TEST_F(DatagramSocketRouteCacheTest, GetNulloptCachesConnectedAddr) {
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());

  std::optional<SocketAddress> to;
  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_TRUE(server_.TakeSendMsgPreflightCalled());

  ASSERT_NO_FATAL_FAILURE(GetFromCacheAssertSuccess(to));
  ASSERT_FALSE(server_.TakeSendMsgPreflightCalled());
}

class RawSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::eventpair::create(0u, &event_client_, &event_server_));

    zx::status server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(server_end.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*server_end), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_raw_socket_init(&storage_, TakeEventClient(), TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  zx::eventpair TakeEventClient() { return std::move(event_client_); }
  fidl::ClientEnd<fsocket_raw::Socket> TakeClientEnd() { return std::move(client_end_); }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx::eventpair event_client_, event_server_;
  fidl::ClientEnd<fsocket_raw::Socket> client_end_;
  zxio_tests::RawSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

TEST_F(RawSocketTest, Basic) { Init(); }

TEST_F(RawSocketTest, Release) {
  Init();
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(RawSocketTest, Borrow) {
  Init();
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

TEST_F(RawSocketTest, CreateWithType) {
  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_RAW_SOCKET,
                                  TakeEventClient().release(),
                                  TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}

class PacketSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::eventpair::create(0u, &event_client_, &event_server_));

    zx::status server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(server_end.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*server_end), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_packet_socket_init(&storage_, TakeEventClient(), TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  zx::eventpair TakeEventClient() { return std::move(event_client_); }
  fidl::ClientEnd<fsocket_packet::Socket> TakeClientEnd() { return std::move(client_end_); }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx::eventpair event_client_, event_server_;
  fidl::ClientEnd<fsocket_packet::Socket> client_end_;
  zxio_tests::PacketSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

TEST_F(PacketSocketTest, Basic) { Init(); }

TEST_F(PacketSocketTest, Release) {
  Init();
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(PacketSocketTest, Borrow) {
  Init();
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

TEST_F(PacketSocketTest, CreateWithType) {
  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_PACKET_SOCKET,
                                  TakeEventClient().release(),
                                  TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}

}  // namespace
