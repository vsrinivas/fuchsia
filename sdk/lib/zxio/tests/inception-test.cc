// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.pty/cpp/wire_test_base.h>
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/cpp/inception.h>
#include <string.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/tests/test_directory_server_base.h"
#include "sdk/lib/zxio/tests/test_file_server_base.h"
#include "sdk/lib/zxio/tests/test_socket_server.h"

TEST(CreateWithAllocator, ErrorAllocator) {
  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    return ZX_ERR_INVALID_ARGS;
  };
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));
  void* context;
  ASSERT_STATUS(zxio_create_with_allocator(std::move(socket0), allocator, &context),
                ZX_ERR_NO_MEMORY);

  // Make sure that the handle is closed.
  zx_signals_t pending = 0;
  ASSERT_STATUS(socket1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_CHANNEL_PEER_CLOSED, ZX_CHANNEL_PEER_CLOSED)
      << "pending is " << std::showbase << std::hex << pending;
}

TEST(CreateWithAllocator, BadAllocator) {
  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    *out_storage = nullptr;
    return ZX_OK;
  };
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));
  void* context;
  ASSERT_STATUS(zxio_create_with_allocator(std::move(socket0), allocator, &context),
                ZX_ERR_NO_MEMORY);

  // Make sure that the handle is closed.
  zx_signals_t pending = 0;
  ASSERT_STATUS(socket1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_CHANNEL_PEER_CLOSED, ZX_CHANNEL_PEER_CLOSED)
      << "pending is " << std::showbase << std::hex << pending;
}

struct VmoWrapper {
  int32_t tag;
  zxio_storage_t storage;
};

TEST(CreateWithAllocator, Vmo) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(1024, 0u, &vmo));

  uint32_t data = 0x1a2a3a4a;
  ASSERT_OK(vmo.write(&data, 0u, sizeof(data)));

  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    if (type != ZXIO_OBJECT_TYPE_VMO) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    auto* wrapper = new VmoWrapper{
        .tag = 0x42,
        .storage = {},
    };
    *out_storage = &(wrapper->storage);
    *out_context = wrapper;
    return ZX_OK;
  };

  void* context = nullptr;
  ASSERT_OK(zxio_create_with_allocator(std::move(vmo), allocator, &context));
  ASSERT_NE(nullptr, context);
  std::unique_ptr<VmoWrapper> wrapper(static_cast<VmoWrapper*>(context));
  ASSERT_EQ(wrapper->tag, 0x42);

  zxio_t* io = &(wrapper->storage.io);

  uint32_t buffer = 0;
  size_t actual = 0;
  zxio_read(io, &buffer, sizeof(buffer), 0u, &actual);
  ASSERT_EQ(actual, sizeof(buffer));
  ASSERT_EQ(buffer, data);

  ASSERT_OK(zxio_close(io));
}

namespace {

class TestDirectoryServer final : public zxio_tests::TestDirectoryServerBase {
  void Sync(SyncCompleter::Sync& completer) final { completer.ReplySuccess(); }
};

}  // namespace

TEST(CreateWithAllocator, Directory) {
  zx::status dir_ends = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(dir_ends.status_value());
  auto [dir_client, dir_server] = std::move(dir_ends.value());

  auto node_info = fuchsia_io::wire::NodeInfoDeprecated::WithDirectory({});

  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    if (type != ZXIO_OBJECT_TYPE_DIR) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    *out_storage = new zxio_storage_t;
    *out_context = *out_storage;
    return ZX_OK;
  };

  async::Loop dir_control_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  TestDirectoryServer server;
  fidl::BindServer(dir_control_loop.dispatcher(), std::move(dir_server), &server);
  dir_control_loop.StartThread("dir_control_thread");

  void* context = nullptr;
  fidl::ClientEnd<fuchsia_io::Node> node_client(dir_client.TakeChannel());
  ASSERT_OK(zxio_create_with_allocator(std::move(node_client), node_info, allocator, &context));
  ASSERT_NE(context, nullptr);

  std::unique_ptr<zxio_storage_t> storage(static_cast<zxio_storage_t*>(context));
  zxio_t* zxio = &(storage->io);

  // Sanity check the zxio by sending a sync operation to the server.
  EXPECT_OK(zxio_sync(zxio));

  ASSERT_OK(zxio_close(zxio));

  dir_control_loop.Shutdown();
}

TEST(CreateWithAllocator, File) {
  zx::status file_ends = fidl::CreateEndpoints<fuchsia_io::File>();
  ASSERT_OK(file_ends.status_value());
  auto [file_client, file_server] = std::move(file_ends.value());

  zx::event file_event;
  ASSERT_OK(zx::event::create(0u, &file_event));
  fuchsia_io::wire::FileObject file = {
      .event = std::move(file_event),
  };
  auto node_info = fuchsia_io::wire::NodeInfoDeprecated::WithFile(
      fidl::ObjectView<decltype(file)>::FromExternal(&file));

  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    if (type != ZXIO_OBJECT_TYPE_FILE) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    *out_storage = new zxio_storage_t;
    *out_context = *out_storage;
    return ZX_OK;
  };

  async::Loop file_control_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zxio_tests::TestReadFileServer server;
  fidl::BindServer(file_control_loop.dispatcher(), std::move(file_server), &server);
  file_control_loop.StartThread("file_control_thread");

  void* context = nullptr;
  fidl::ClientEnd<fuchsia_io::Node> node_client(file_client.TakeChannel());
  ASSERT_OK(zxio_create_with_allocator(std::move(node_client), node_info, allocator, &context));
  ASSERT_NE(context, nullptr);

  // The event in node_info should be consumed by zxio.
  EXPECT_FALSE(node_info.file().event.is_valid());

  std::unique_ptr<zxio_storage_t> storage(static_cast<zxio_storage_t*>(context));
  zxio_t* zxio = &(storage->io);

  // Sanity check the zxio by reading some test data from the server.
  char buffer[sizeof(zxio_tests::TestReadFileServer::kTestData)];
  size_t actual = 0u;

  ASSERT_OK(zxio_read(zxio, buffer, sizeof(buffer), 0u, &actual));

  EXPECT_EQ(sizeof(buffer), actual);
  EXPECT_BYTES_EQ(buffer, zxio_tests::TestReadFileServer::kTestData, sizeof(buffer));

  ASSERT_OK(zxio_close(zxio));

  file_control_loop.Shutdown();
}

class TestServiceNodeServer : public fidl::testing::WireTestBase<fuchsia_io::Node> {
 public:
  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE() << "unexpected message received: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync& completer) final {
    completer.ReplySuccess();
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }
};

TEST(CreateWithAllocator, Service) {
  zx::status node_ends = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_OK(node_ends.status_value());
  auto [node_client, node_server] = std::move(node_ends.value());

  auto node_info = fuchsia_io::wire::NodeInfoDeprecated::WithService({});

  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    if (type != ZXIO_OBJECT_TYPE_SERVICE) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    *out_storage = new zxio_storage_t;
    *out_context = *out_storage;
    return ZX_OK;
  };

  async::Loop service_control_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  TestServiceNodeServer server;
  fidl::BindServer(service_control_loop.dispatcher(), std::move(node_server), &server);
  service_control_loop.StartThread("service_control_thread");

  void* context = nullptr;
  ASSERT_OK(zxio_create_with_allocator(std::move(node_client), node_info, allocator, &context));
  ASSERT_NE(context, nullptr);

  std::unique_ptr<zxio_storage_t> storage(static_cast<zxio_storage_t*>(context));
  zxio_t* zxio = &(storage->io);

  ASSERT_OK(zxio_close(zxio));

  service_control_loop.Shutdown();
}

class TestTtyServer : public fidl::testing::WireTestBase<fuchsia_hardware_pty::Device> {
 public:
  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE() << "unexpected message received: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync& completer) final {
    completer.ReplySuccess();
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }
};

TEST(CreateWithAllocator, Tty) {
  zx::status node_ends = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_OK(node_ends.status_value());
  auto [node_client, node_server] = std::move(node_ends.value());

  zx::eventpair event0, event1;
  ASSERT_OK(zx::eventpair::create(0, &event0, &event1));

  auto node_info = fuchsia_io::wire::NodeInfoDeprecated::WithTty({
      .event = std::move(event1),
  });

  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    if (type != ZXIO_OBJECT_TYPE_TTY) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    *out_storage = new zxio_storage_t;
    *out_context = *out_storage;
    return ZX_OK;
  };

  async::Loop tty_control_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  TestTtyServer server;
  fidl::ServerEnd<fuchsia_hardware_pty::Device> tty_server(node_server.TakeChannel());
  fidl::BindServer(tty_control_loop.dispatcher(), std::move(tty_server), &server);
  tty_control_loop.StartThread("tty_control_thread");

  void* context = nullptr;
  ASSERT_OK(zxio_create_with_allocator(std::move(node_client), node_info, allocator, &context));
  ASSERT_NE(context, nullptr);

  // The event in node_info should be consumed by zxio.
  EXPECT_FALSE(node_info.tty().event.is_valid());

  std::unique_ptr<zxio_storage_t> storage(static_cast<zxio_storage_t*>(context));
  zxio_t* zxio = &(storage->io);

  // Closing the zxio object should close our eventpair's peer event.
  zx_signals_t pending = 0;
  ASSERT_STATUS(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_NE(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED)
      << "pending is " << std::showbase << std::hex << pending;

  ASSERT_OK(zxio_close(zxio));

  ASSERT_STATUS(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED)
      << "pending is " << std::showbase << std::hex << pending;

  tty_control_loop.Shutdown();
}

TEST(CreateWithAllocator, PacketSocket) {
  zx::status socket_ends = fidl::CreateEndpoints<fuchsia_posix_socket_packet::Socket>();
  ASSERT_OK(socket_ends.status_value());
  auto [socket_client, socket_server] = std::move(socket_ends.value());

  zx::eventpair event0, event1;
  ASSERT_OK(zx::eventpair::create(0, &event0, &event1));

  auto node_info = fuchsia_io::wire::NodeInfoDeprecated::WithPacketSocket({
      .event = std::move(event1),
  });

  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    if (type != ZXIO_OBJECT_TYPE_PACKET_SOCKET) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    *out_storage = new zxio_storage_t;
    *out_context = *out_storage;
    return ZX_OK;
  };

  async::Loop control_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zxio_tests::PacketSocketServer server;
  fidl::BindServer(control_loop.dispatcher(), std::move(socket_server), &server);
  control_loop.StartThread("packet_socket_control_thread");

  void* context = nullptr;
  ASSERT_OK(
      zxio_create_with_allocator(fidl::ClientEnd<fuchsia_io::Node>(socket_client.TakeChannel()),
                                 node_info, allocator, &context));
  ASSERT_NE(context, nullptr);

  // The event in node_info should be consumed by zxio.
  EXPECT_FALSE(node_info.packet_socket().event.is_valid());

  std::unique_ptr<zxio_storage_t> storage(static_cast<zxio_storage_t*>(context));
  zxio_t* zxio = &(storage->io);

  // Closing the zxio object should close our eventpair's peer event.
  zx_signals_t pending = 0;
  ASSERT_STATUS(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_NE(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED)
      << "pending is " << std::showbase << std::hex << pending;

  ASSERT_OK(zxio_close(zxio));

  ASSERT_STATUS(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED)
      << "pending is " << std::showbase << std::hex << pending;

  control_loop.Shutdown();
}

TEST(CreateWithAllocator, RawSocket) {
  zx::status socket_ends = fidl::CreateEndpoints<fuchsia_posix_socket_raw::Socket>();
  ASSERT_OK(socket_ends.status_value());
  auto [socket_client, socket_server] = std::move(socket_ends.value());

  zx::eventpair event0, event1;
  ASSERT_OK(zx::eventpair::create(0, &event0, &event1));

  auto node_info = fuchsia_io::wire::NodeInfoDeprecated::WithRawSocket({
      .event = std::move(event1),
  });

  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    if (type != ZXIO_OBJECT_TYPE_RAW_SOCKET) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    *out_storage = new zxio_storage_t;
    *out_context = *out_storage;
    return ZX_OK;
  };

  async::Loop control_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zxio_tests::RawSocketServer server;
  fidl::BindServer(control_loop.dispatcher(), std::move(socket_server), &server);
  control_loop.StartThread("raw_socket_control_thread");

  void* context = nullptr;
  ASSERT_OK(
      zxio_create_with_allocator(fidl::ClientEnd<fuchsia_io::Node>(socket_client.TakeChannel()),
                                 node_info, allocator, &context));
  ASSERT_NE(context, nullptr);

  // The event in node_info should be consumed by zxio.
  EXPECT_FALSE(node_info.raw_socket().event.is_valid());

  std::unique_ptr<zxio_storage_t> storage(static_cast<zxio_storage_t*>(context));
  zxio_t* zxio = &(storage->io);

  // Closing the zxio object should close our eventpair's peer event.
  zx_signals_t pending = 0;
  ASSERT_STATUS(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_NE(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED)
      << "pending is " << std::showbase << std::hex << pending;

  ASSERT_OK(zxio_close(zxio));

  ASSERT_STATUS(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED)
      << "pending is " << std::showbase << std::hex << pending;

  control_loop.Shutdown();
}

TEST(CreateWithAllocator, SynchronousDatagramSocket) {
  zx::status socket_ends = fidl::CreateEndpoints<fuchsia_posix_socket::SynchronousDatagramSocket>();
  ASSERT_OK(socket_ends.status_value());
  auto [socket_client, socket_server] = std::move(socket_ends.value());

  zx::eventpair event0, event1;
  ASSERT_OK(zx::eventpair::create(0, &event0, &event1));

  auto node_info = fuchsia_io::wire::NodeInfoDeprecated::WithSynchronousDatagramSocket({
      .event = std::move(event1),
  });

  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    if (type != ZXIO_OBJECT_TYPE_SYNCHRONOUS_DATAGRAM_SOCKET) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    *out_storage = new zxio_storage_t;
    *out_context = *out_storage;
    return ZX_OK;
  };

  async::Loop control_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zxio_tests::SynchronousDatagramSocketServer server;
  fidl::BindServer(control_loop.dispatcher(), std::move(socket_server), &server);
  control_loop.StartThread("synchronous_datagram_socket_control_thread");

  void* context = nullptr;
  ASSERT_OK(
      zxio_create_with_allocator(fidl::ClientEnd<fuchsia_io::Node>(socket_client.TakeChannel()),
                                 node_info, allocator, &context));
  ASSERT_NE(context, nullptr);

  // The event in node_info should be consumed by zxio.
  EXPECT_FALSE(node_info.synchronous_datagram_socket().event.is_valid());

  std::unique_ptr<zxio_storage_t> storage(static_cast<zxio_storage_t*>(context));
  zxio_t* zxio = &(storage->io);

  // Closing the zxio object should close our eventpair's peer event.
  zx_signals_t pending = 0;
  ASSERT_STATUS(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_NE(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED)
      << "pending is " << std::showbase << std::hex << pending;

  ASSERT_OK(zxio_close(zxio));

  ASSERT_STATUS(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED)
      << "pending is " << std::showbase << std::hex << pending;

  control_loop.Shutdown();
}

TEST(CreateWithAllocator, DatagramSocket) {
  zx::status socket_ends = fidl::CreateEndpoints<fuchsia_posix_socket::DatagramSocket>();
  ASSERT_OK(socket_ends.status_value());
  auto [socket_client, socket_server] = std::move(socket_ends.value());

  zx::socket socket, peer;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &socket, &peer));
  zx_info_socket_t info;
  ASSERT_OK(socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));

  fuchsia_io::wire::DatagramSocket datagram_info{.socket = std::move(socket)};
  fuchsia_io::wire::NodeInfoDeprecated node_info =
      fuchsia_io::wire::NodeInfoDeprecated::WithDatagramSocket(
          fidl::ObjectView<fuchsia_io::wire::DatagramSocket>::FromExternal(&datagram_info));

  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    if (type != ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    *out_storage = new zxio_storage_t;
    *out_context = *out_storage;
    return ZX_OK;
  };

  async::Loop control_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zxio_tests::DatagramSocketServer server;
  fidl::BindServer(control_loop.dispatcher(), std::move(socket_server), &server);
  control_loop.StartThread("datagram_socket_control_thread");

  void* context = nullptr;
  ASSERT_OK(
      zxio_create_with_allocator(fidl::ClientEnd<fuchsia_io::Node>(socket_client.TakeChannel()),
                                 node_info, allocator, &context));
  ASSERT_NE(context, nullptr);

  // The socket in node_info should be consumed by zxio.
  EXPECT_FALSE(node_info.datagram_socket().socket.is_valid());

  std::unique_ptr<zxio_storage_t> storage(static_cast<zxio_storage_t*>(context));
  zxio_t* zxio = &(storage->io);

  ASSERT_OK(zxio_close(zxio));
  control_loop.Shutdown();
}

TEST(CreateWithAllocator, StreamSocket) {
  zx::status socket_ends = fidl::CreateEndpoints<fuchsia_posix_socket::StreamSocket>();
  ASSERT_OK(socket_ends.status_value());
  auto [socket_client, socket_server] = std::move(socket_ends.value());

  zx::socket socket, peer;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket, &peer));
  zx_info_socket_t info;
  ASSERT_OK(socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));

  fuchsia_io::wire::StreamSocket stream_info{.socket = std::move(socket)};
  fuchsia_io::wire::NodeInfoDeprecated node_info =
      fuchsia_io::wire::NodeInfoDeprecated::WithStreamSocket(std::move(stream_info));

  auto allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage, void** out_context) {
    if (type != ZXIO_OBJECT_TYPE_STREAM_SOCKET) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    *out_storage = new zxio_storage_t;
    *out_context = *out_storage;
    return ZX_OK;
  };

  async::Loop control_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zxio_tests::StreamSocketServer server;
  fidl::BindServer(control_loop.dispatcher(), std::move(socket_server), &server);
  control_loop.StartThread("stream_socket_control_thread");

  void* context = nullptr;
  ASSERT_OK(
      zxio_create_with_allocator(fidl::ClientEnd<fuchsia_io::Node>(socket_client.TakeChannel()),
                                 node_info, allocator, &context));
  ASSERT_NE(context, nullptr);

  // The socket in node_info should be consumed by zxio.
  EXPECT_FALSE(node_info.stream_socket().socket.is_valid());

  std::unique_ptr<zxio_storage_t> storage(static_cast<zxio_storage_t*>(context));
  zxio_t* zxio = &(storage->io);

  ASSERT_OK(zxio_close(zxio));
  control_loop.Shutdown();
}
