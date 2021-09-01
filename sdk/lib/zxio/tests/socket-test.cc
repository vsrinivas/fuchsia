// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket.raw/cpp/wire_test_base.h>
#include <fidl/fuchsia.posix.socket/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/socket.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/zxio.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/private.h"

namespace {

class DatagramSocketServer final : public fuchsia_posix_socket::testing::DatagramSocket_TestBase {
 public:
  DatagramSocketServer() = default;

  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) final {
    ADD_FAILURE("unexpected message received: %s", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) final {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseRequestView request, CloseCompleter::Sync& completer) final {
    completer.Reply(ZX_OK);
    completer.Close(ZX_OK);
  }
};

class DatagramSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::eventpair::create(0u, &event0_, &event1_));

    zx::status node_server = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(node_server.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*node_server), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_datagram_socket_init(&storage_, TakeEvent(), TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  zx::eventpair TakeEvent() { return std::move(event0_); }
  fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> TakeClientEnd() {
    return std::move(client_end_);
  }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx::eventpair event0_, event1_;
  fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> client_end_;
  DatagramSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

}  // namespace

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
                                  TakeEvent().release(), TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}

TEST_F(DatagramSocketTest, CreateWithTypeWrapper) {
  ASSERT_OK(zxio::CreateDatagramSocket(storage(), TakeEvent(), TakeClientEnd()));
  ASSERT_OK(zxio_close(&storage()->io));
}

namespace {

class StreamSocketServer final : public fuchsia_posix_socket::testing::StreamSocket_TestBase {
 public:
  StreamSocketServer() = default;

  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) final {
    ADD_FAILURE("unexpected message received: %s", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    completer.Reply(ZX_OK);
    completer.Close(ZX_OK);
  }
};

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
    ASSERT_OK(zxio_stream_socket_init(&storage_, TakeSocket(), TakeClientEnd(), info()));
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
  fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> TakeClientEnd() {
    return std::move(client_end_);
  }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx_info_socket_t info_;
  zx::socket socket_, peer_;
  fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client_end_;
  StreamSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

}  // namespace

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
                                  TakeClientEnd().TakeChannel().release(), &info()));
  ASSERT_OK(zxio_close(&storage()->io));
}

TEST_F(StreamSocketTest, CreateWithTypeWrapper) {
  ASSERT_OK(zxio::CreateStreamSocket(storage(), TakeSocket(), TakeClientEnd(), info()));
  ASSERT_OK(zxio_close(&storage()->io));
}

namespace {

class RawSocketServer final : public fuchsia_posix_socket_raw::testing::Socket_TestBase {
 public:
  RawSocketServer() = default;

  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) final {
    ADD_FAILURE("unexpected message received: %s", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) final {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseRequestView request, CloseCompleter::Sync& completer) final {
    completer.Reply(ZX_OK);
    completer.Close(ZX_OK);
  }
};

class RawSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::eventpair::create(0u, &event_client_, &event_server_));

    auto server_end = fidl::CreateEndpoints(&client_end_);
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
  fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> TakeClientEnd() {
    return std::move(client_end_);
  }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx::eventpair event_client_, event_server_;
  fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> client_end_;
  RawSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

}  // namespace

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

TEST_F(RawSocketTest, CreateWithTypeWrapper) {
  ASSERT_OK(zxio::CreateRawSocket(storage(), TakeEventClient(), TakeClientEnd()));
  ASSERT_OK(zxio_close(&storage()->io));
}
