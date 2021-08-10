// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/posix/socket/llcpp/fidl_test_base.h>
#include <fuchsia/posix/socket/raw/llcpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/socket.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/zxio.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

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
  DatagramSocketTest() : control_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() final {
    zx::eventpair event_client;
    ASSERT_OK(zx::eventpair::create(0u, &event_client, &event_server_));

    auto node_ends = fidl::CreateEndpoints<fuchsia_posix_socket::DatagramSocket>();
    ASSERT_OK(node_ends.status_value());
    fidl::ClientEnd client = std::move(node_ends->client);

    fidl::BindServer(control_loop_.dispatcher(), std::move(node_ends->server), &server_);
    control_loop_.StartThread("control");

    ASSERT_OK(zxio_datagram_socket_init(&storage_, std::move(event_client), std::move(client)));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    ASSERT_OK(zxio_close(zxio_));
    control_loop_.Shutdown();
  }

 protected:
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_;
  zx::eventpair event_server_;
  DatagramSocketServer server_;
  async::Loop control_loop_;
};

}  // namespace

TEST_F(DatagramSocketTest, Basic) {}

TEST_F(DatagramSocketTest, Release) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(DatagramSocketTest, Borrow) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
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
  StreamSocketTest() : control_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() final {
    zx::socket socket;
    zx_info_socket_t info;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket, &peer_));
    ASSERT_OK(socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));

    auto node_ends = fidl::CreateEndpoints<fuchsia_posix_socket::StreamSocket>();
    ASSERT_OK(node_ends.status_value());
    fidl::ClientEnd client = std::move(node_ends->client);

    fidl::BindServer(control_loop_.dispatcher(), std::move(node_ends->server), &server_);
    control_loop_.StartThread("control");

    ASSERT_OK(zxio_stream_socket_init(&storage_, std::move(socket), std::move(client), info));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    ASSERT_OK(zxio_close(zxio_));
    control_loop_.Shutdown();
  }

 protected:
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_;
  zx::socket peer_;
  StreamSocketServer server_;
  async::Loop control_loop_;
};

}  // namespace

TEST(StreamSocket, Basic) {}

TEST_F(StreamSocketTest, Release) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(StreamSocketTest, Borrow) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
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
  RawSocketTest() : control_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() final {
    zx::eventpair event_client;
    ASSERT_OK(zx::eventpair::create(0u, &event_client, &event_server_));

    auto node_ends = fidl::CreateEndpoints<fuchsia_posix_socket_raw::Socket>();
    ASSERT_OK(node_ends.status_value());
    fidl::ClientEnd client = std::move(node_ends->client);

    fidl::BindServer(control_loop_.dispatcher(), std::move(node_ends->server), &server_);
    control_loop_.StartThread("control");

    ASSERT_OK(zxio_raw_socket_init(&storage_, std::move(event_client), std::move(client)));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    ASSERT_OK(zxio_close(zxio_));
    control_loop_.Shutdown();
  }

 protected:
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_;
  zx::eventpair event_server_;
  RawSocketServer server_;
  async::Loop control_loop_;
};

}  // namespace

TEST_F(RawSocketTest, Basic) {}

TEST_F(RawSocketTest, Release) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(RawSocketTest, Borrow) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}
