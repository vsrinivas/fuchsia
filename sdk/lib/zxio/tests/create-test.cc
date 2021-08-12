// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <lib/zxio/types.h>
#include <lib/zxio/zxio.h>
#include <zircon/syscalls/types.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/tests/test_file_server_base.h"
#include "sdk/lib/zxio/tests/test_node_server.h"

TEST(Create, InvalidArgs) {
  ASSERT_EQ(zxio_create(ZX_HANDLE_INVALID, nullptr), ZX_ERR_INVALID_ARGS);

  zxio_storage_t storage;
  ASSERT_EQ(zxio_create(ZX_HANDLE_INVALID, &storage), ZX_ERR_INVALID_ARGS);

  zx::channel channel0, channel1;
  ASSERT_OK(zx::channel::create(0u, &channel0, &channel1));
  ASSERT_EQ(zxio_create(channel0.release(), nullptr), ZX_ERR_INVALID_ARGS);

  // Make sure that the handle is closed.
  zx_signals_t pending = 0;
  ASSERT_EQ(channel1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_CHANNEL_PEER_CLOSED, ZX_CHANNEL_PEER_CLOSED);
}

TEST(Create, NotSupported) {
  zx::event event;
  ASSERT_OK(zx::event::create(0u, &event));
  zxio_storage_t storage;
  ASSERT_EQ(zxio_create(event.release(), &storage), ZX_ERR_NOT_SUPPORTED);
  zxio_t* io = &storage.io;
  zx::handle handle;
  ASSERT_OK(zxio_release(io, handle.reset_and_get_address()));
  ASSERT_OK(zxio_close(io));

  zx_info_handle_basic_t info = {};
  ASSERT_OK(handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.type, ZX_OBJ_TYPE_EVENT);
}

// Tests that calling zxio_create_with_type() with an invalid storage pointer
// still closes all of the handles known for the type.
TEST(CreateWithTypeInvalidStorageClosesHandles, DatagramSocket) {
  zx::eventpair event0, event1;
  ASSERT_OK(zx::eventpair::create(0, &event0, &event1));
  zx::channel channel0, channel1;
  ASSERT_OK(zx::channel::create(0, &channel0, &channel1));

  ASSERT_EQ(zxio_create_with_type(nullptr, ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET, event0.release(),
                                  channel0.release()),
            ZX_ERR_INVALID_ARGS);

  zx_signals_t pending = 0;
  ASSERT_EQ(event1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED);

  ASSERT_EQ(channel1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_CHANNEL_PEER_CLOSED, ZX_CHANNEL_PEER_CLOSED);
}

TEST(CreateWithTypeInvalidStorageClosesHandles, Directory) {
  zx::channel channel0, channel1;
  ASSERT_OK(zx::channel::create(0, &channel0, &channel1));

  ASSERT_EQ(zxio_create_with_type(nullptr, ZXIO_OBJECT_TYPE_DIR, channel0.release()),
            ZX_ERR_INVALID_ARGS);

  zx_signals_t pending = 0;
  ASSERT_EQ(channel1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_CHANNEL_PEER_CLOSED, ZX_CHANNEL_PEER_CLOSED);
}

TEST(CreateWithTypeInvalidStorageClosesHandles, Node) {
  zx::channel channel0, channel1;
  ASSERT_OK(zx::channel::create(0, &channel0, &channel1));

  ASSERT_EQ(zxio_create_with_type(nullptr, ZXIO_OBJECT_TYPE_NODE, channel0.release()),
            ZX_ERR_INVALID_ARGS);

  zx_signals_t pending = 0;
  ASSERT_EQ(channel1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_CHANNEL_PEER_CLOSED, ZX_CHANNEL_PEER_CLOSED);
}

TEST(CreateWithTypeInvalidStorageClosesHandles, StreamSocket) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket0, &socket1));
  zx::channel channel0, channel1;
  ASSERT_OK(zx::channel::create(0, &channel0, &channel1));

  ASSERT_EQ(zxio_create_with_type(nullptr, ZXIO_OBJECT_TYPE_STREAM_SOCKET, socket0.release(),
                                  channel0.release(), nullptr),
            ZX_ERR_INVALID_ARGS);

  zx_signals_t pending = 0;
  ASSERT_EQ(socket1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_SOCKET_PEER_CLOSED, ZX_SOCKET_PEER_CLOSED);

  ASSERT_EQ(channel1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_CHANNEL_PEER_CLOSED, ZX_CHANNEL_PEER_CLOSED);
}

TEST(CreateWithTypeInvalidStorageClosesHandles, Pipe) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket0, &socket1));

  ASSERT_EQ(zxio_create_with_type(nullptr, ZXIO_OBJECT_TYPE_PIPE, socket0.release()),
            ZX_ERR_INVALID_ARGS);

  // Make sure that the handle is closed.
  zx_signals_t pending = 0;
  ASSERT_EQ(socket1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_SOCKET_PEER_CLOSED, ZX_SOCKET_PEER_CLOSED);
}

TEST(CreateWithTypeInvalidStorageClosesHandles, RawSocket) {
  zx::eventpair event0, event1;
  ASSERT_OK(zx::eventpair::create(0, &event0, &event1));
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket0, &socket1));

  ASSERT_EQ(zxio_create_with_type(nullptr, ZXIO_OBJECT_TYPE_RAW_SOCKET, event0.release(),
                                  socket0.release()),
            ZX_ERR_INVALID_ARGS);

  // Make sure that the handle is closed.
  zx_signals_t pending = 0;
  ASSERT_EQ(event1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED);

  ASSERT_EQ(socket1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_SOCKET_PEER_CLOSED, ZX_SOCKET_PEER_CLOSED);
}

TEST(CreateWithTypeInvalidStorageClosesHandles, Vmo) {
  zx::vmo parent, child;
  ASSERT_OK(zx::vmo::create(4096, 0, &parent));
  ASSERT_OK(parent.create_child(ZX_VMO_CHILD_SLICE, 0, 4096, &child));
  zx::stream stream;
  ASSERT_OK(zx::stream::create(0, child, 0, &stream));

  ASSERT_EQ(zxio_create_with_type(nullptr, ZXIO_OBJECT_TYPE_VMO, child.release(), stream.release()),
            ZX_ERR_INVALID_ARGS);

  // Make sure that the handle is closed.
  zx_signals_t pending = 0;
  ASSERT_EQ(parent.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_VMO_ZERO_CHILDREN, ZX_VMO_ZERO_CHILDREN);
}

template <typename NodeServer>
class CreateTestBase : public zxtest::Test {
 protected:
  CreateTestBase() : control_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  using ProtocolType = typename NodeServer::_EnclosingProtocol;

  void SetUp() final {
    auto node_ends = fidl::CreateEndpoints<ProtocolType>();
    ASSERT_OK(node_ends.status_value());
    node_client_end_ = std::move(node_ends->client);
    node_server_end_ = std::move(node_ends->server);
  }

  void TearDown() final { control_loop_.Shutdown(); }

  zx::channel TakeClientChannel() { return node_client_end_.TakeChannel(); }

  void StartServerThread() {
    fidl::BindServer(control_loop_.dispatcher(), std::move(node_server_end_), &node_server_);
    control_loop_.StartThread("control");
  }

  zxio_t* zxio() { return &storage_.io; }
  zxio_storage_t* storage() { return &storage_; }

  void SendOnOpenEvent(fuchsia_io::wire::NodeInfo node_info) {
    fidl::WireEventSender<ProtocolType> sender;
    sender.server_end() = std::move(node_server_end_);
    ASSERT_OK(sender.OnOpen(ZX_OK, std::move(node_info)));
    node_server_end_ = std::move(sender.server_end());
  }

  NodeServer& node_server() { return node_server_; }

 private:
  zxio_storage_t storage_;
  fidl::ClientEnd<ProtocolType> node_client_end_;
  fidl::ServerEnd<ProtocolType> node_server_end_;
  NodeServer node_server_;
  async::Loop control_loop_;
};

using CreateTest = CreateTestBase<zxio_tests::DescribeNodeServer>;
using CreateWithOnOpenTest = CreateTestBase<zxio_tests::CloseOnlyNodeServer>;

using DescribeRequestView = fidl::WireServer<::fuchsia_io::Node>::DescribeRequestView;
using DescribeCompleter = fidl::WireServer<::fuchsia_io::Node>::DescribeCompleter;

TEST_F(CreateTest, Device) {
  zx::eventpair event0, event1;
  ASSERT_OK(zx::eventpair::create(0, &event0, &event1));
  node_server().set_describe_function(
      [event1 = std::move(event1)](DescribeRequestView request,
                                   DescribeCompleter::Sync& completer) mutable {
        fuchsia_io::wire::Device device = {.event = std::move(event1)};
        fidl::Arena fidl_allocator;
        auto node_info = fuchsia_io::wire::NodeInfo::WithDevice(fidl_allocator, std::move(device));
        completer.Reply(std::move(node_info));
      });

  StartServerThread();

  ASSERT_OK(zxio_create(TakeClientChannel().release(), storage()));

  // Closing the zxio object should close our eventpair's peer event.
  zx_signals_t pending = 0;
  ASSERT_EQ(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_NE(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "pending is %u", pending);

  ASSERT_OK(zxio_close(zxio()));

  ASSERT_EQ(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "pending is %u", pending);
}

TEST_F(CreateWithOnOpenTest, Device) {
  zx::eventpair event0, event1;

  ASSERT_OK(zx::eventpair::create(0, &event0, &event1));
  fuchsia_io::wire::Device device = {.event = std::move(event1)};
  fidl::Arena fidl_allocator;
  auto node_info = fuchsia_io::wire::NodeInfo::WithDevice(fidl_allocator, std::move(device));

  SendOnOpenEvent(std::move(node_info));

  ASSERT_OK(zxio_create_with_on_open(TakeClientChannel().release(), storage()));

  // Closing the zxio object should close our eventpair's peer event.
  zx_signals_t pending = 0;
  ASSERT_EQ(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_NE(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "pending is %u", pending);

  StartServerThread();

  ASSERT_OK(zxio_close(zxio()));

  ASSERT_EQ(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "pending is %u", pending);
}

class SyncNodeServer : public zxio_tests::DescribeNodeServer {
 public:
  void Sync(SyncRequestView request, SyncCompleter::Sync& completer) final {
    completer.Reply(ZX_OK);
  }
};

using CreateDirectoryTest = CreateTestBase<SyncNodeServer>;

TEST_F(CreateDirectoryTest, Directory) {
  node_server().set_describe_function(
      [](DescribeRequestView request, DescribeCompleter::Sync& completer) mutable {
        fidl::Arena fidl_allocator;
        auto node_info = fuchsia_io::wire::NodeInfo::WithDirectory(fidl_allocator);
        completer.Reply(std::move(node_info));
      });

  StartServerThread();

  ASSERT_OK(zxio_create(TakeClientChannel().release(), storage()));

  EXPECT_OK(zxio_sync(zxio()));

  ASSERT_OK(zxio_close(zxio()));
}

TEST_F(CreateDirectoryTest, DirectoryWithType) {
  StartServerThread();

  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_DIR, TakeClientChannel().release()));

  EXPECT_OK(zxio_sync(zxio()));

  ASSERT_OK(zxio_close(zxio()));
}

TEST_F(CreateDirectoryTest, DirectoryWithTypeWrapper) {
  StartServerThread();

  ASSERT_OK(zxio::CreateDirectory(storage(),
                                  fidl::ClientEnd<fuchsia_io::Directory>(TakeClientChannel())));

  EXPECT_OK(zxio_sync(zxio()));

  ASSERT_OK(zxio_close(zxio()));
}

using CreateDirectoryWithOnOpenTest = CreateTestBase<SyncNodeServer>;

TEST_F(CreateDirectoryWithOnOpenTest, Directory) {
  fidl::Arena fidl_allocator;
  auto node_info = fuchsia_io::wire::NodeInfo::WithDirectory(fidl_allocator);

  SendOnOpenEvent(std::move(node_info));

  ASSERT_OK(zxio_create_with_on_open(TakeClientChannel().release(), storage()));

  StartServerThread();

  EXPECT_OK(zxio_sync(zxio()));

  ASSERT_OK(zxio_close(zxio()));
}

class TestFileServerWithDescribe : public zxio_tests::TestReadFileServer {
 public:
  void set_file(fuchsia_io::wire::FileObject file) { file_ = std::move(file); }

 protected:
  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) final {
    fidl::Arena fidl_allocator;
    auto node_info = fuchsia_io::wire::NodeInfo::WithFile(fidl_allocator, std::move(file_));
    completer.Reply(std::move(node_info));
  }

 private:
  fuchsia_io::wire::FileObject file_;
};

using CreateFileTest = CreateTestBase<TestFileServerWithDescribe>;

TEST_F(CreateFileTest, File) {
  zx::event file_event;
  ASSERT_OK(zx::event::create(0u, &file_event));
  zx::stream stream;

  fuchsia_io::wire::FileObject file = {.event = std::move(file_event), .stream = std::move(stream)};
  node_server().set_file(std::move(file));

  StartServerThread();

  ASSERT_OK(zxio_create(TakeClientChannel().release(), storage()));

  // Sanity check the zxio by reading some test data from the server.
  char buffer[sizeof(zxio_tests::TestReadFileServer::kTestData)];
  size_t actual = 0u;

  ASSERT_OK(zxio_read(zxio(), buffer, sizeof(buffer), 0u, &actual));

  EXPECT_EQ(sizeof(buffer), actual);
  EXPECT_BYTES_EQ(buffer, zxio_tests::TestReadFileServer::kTestData, sizeof(buffer));
}

using CreateFileWithOnOpenTest = CreateTestBase<zxio_tests::TestReadFileServer>;

TEST_F(CreateFileWithOnOpenTest, File) {
  zx::event file_event;
  ASSERT_OK(zx::event::create(0u, &file_event));
  zx::stream stream;

  fuchsia_io::wire::FileObject file = {.event = std::move(file_event), .stream = std::move(stream)};
  fidl::Arena fidl_allocator;
  auto node_info = fuchsia_io::wire::NodeInfo::WithFile(fidl_allocator, std::move(file));

  SendOnOpenEvent(std::move(node_info));

  ASSERT_OK(zxio_create_with_on_open(TakeClientChannel().release(), storage()));

  StartServerThread();

  // Sanity check the zxio by reading some test data from the server.
  char buffer[sizeof(zxio_tests::TestReadFileServer::kTestData)];
  size_t actual = 0u;

  ASSERT_OK(zxio_read(zxio(), buffer, sizeof(buffer), 0u, &actual));

  EXPECT_EQ(sizeof(buffer), actual);
  EXPECT_BYTES_EQ(buffer, zxio_tests::TestReadFileServer::kTestData, sizeof(buffer));

  ASSERT_OK(zxio_close(zxio()));
}

TEST_F(CreateTest, Pipe) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));
  fuchsia_io::wire::Pipe pipe = {.socket = std::move(socket0)};
  node_server().set_describe_function(
      [pipe = std::move(pipe)](DescribeRequestView request,
                               DescribeCompleter::Sync& completer) mutable {
        fidl::Arena fidl_allocator;
        auto node_info = fuchsia_io::wire::NodeInfo::WithPipe(fidl_allocator, std::move(pipe));
        completer.Reply(std::move(node_info));
      });

  StartServerThread();

  ASSERT_OK(zxio_create(TakeClientChannel().release(), storage()));

  // Send some data through the kernel socket object and read it through zxio to
  // sanity check that the pipe is functional.
  int32_t data = 0x1a2a3a4a;
  size_t actual = 0u;
  ASSERT_OK(socket1.write(0u, &data, sizeof(data), &actual));
  EXPECT_EQ(actual, sizeof(data));

  int32_t buffer = 0;
  ASSERT_OK(zxio_read(zxio(), &buffer, sizeof(buffer), 0u, &actual));
  EXPECT_EQ(actual, sizeof(buffer));
  EXPECT_EQ(buffer, data);

  ASSERT_OK(zxio_close(zxio()));
}

TEST(CreateWithTypeTest, Pipe) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));

  zx_info_socket_t info = {};
  ASSERT_OK(socket0.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create_with_type(&storage, ZXIO_OBJECT_TYPE_PIPE, socket0.release(), &info));
  zxio_t* zxio = &storage.io;

  // Send some data through the kernel socket object and read it through zxio to
  // sanity check that the pipe is functional.
  int32_t data = 0x1a2a3a4a;
  size_t actual = 0u;
  ASSERT_OK(socket1.write(0u, &data, sizeof(data), &actual));
  EXPECT_EQ(actual, sizeof(data));

  int32_t buffer = 0;
  ASSERT_OK(zxio_read(zxio, &buffer, sizeof(buffer), 0u, &actual));
  EXPECT_EQ(actual, sizeof(buffer));
  EXPECT_EQ(buffer, data);

  ASSERT_OK(zxio_close(zxio));
}

TEST(CreateWithTypeWrapperTest, Pipe) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));

  zx_info_socket_t info = {};
  ASSERT_OK(socket0.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));

  zxio_storage_t storage;
  ASSERT_OK(zxio::CreatePipe(&storage, std::move(socket0), info));
  zxio_t* zxio = &storage.io;

  // Send some data through the kernel socket object and read it through zxio to
  // sanity check that the pipe is functional.
  int32_t data = 0x1a2a3a4a;
  size_t actual = 0u;
  ASSERT_OK(socket1.write(0u, &data, sizeof(data), &actual));
  EXPECT_EQ(actual, sizeof(data));

  int32_t buffer = 0;
  ASSERT_OK(zxio_read(zxio, &buffer, sizeof(buffer), 0u, &actual));
  EXPECT_EQ(actual, sizeof(buffer));
  EXPECT_EQ(buffer, data);

  ASSERT_OK(zxio_close(zxio));
}

TEST_F(CreateWithOnOpenTest, Pipe) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));
  fuchsia_io::wire::Pipe pipe = {.socket = std::move(socket0)};
  fidl::Arena fidl_allocator;
  auto node_info = fuchsia_io::wire::NodeInfo::WithPipe(fidl_allocator, std::move(pipe));

  SendOnOpenEvent(std::move(node_info));

  ASSERT_OK(zxio_create_with_on_open(TakeClientChannel().release(), storage()));

  StartServerThread();

  // Send some data through the kernel socket object and read it through zxio to
  // sanity check that the pipe is functional.
  int32_t data = 0x1a2a3a4a;
  size_t actual = 0u;
  ASSERT_OK(socket1.write(0u, &data, sizeof(data), &actual));
  EXPECT_EQ(actual, sizeof(data));

  int32_t buffer = 0;
  ASSERT_OK(zxio_read(zxio(), &buffer, sizeof(buffer), 0u, &actual));
  EXPECT_EQ(actual, sizeof(buffer));
  EXPECT_EQ(buffer, data);

  ASSERT_OK(zxio_close(zxio()));
}

TEST_F(CreateTest, Service) {
  node_server().set_describe_function(
      [](DescribeRequestView request, DescribeCompleter::Sync& completer) mutable {
        fidl::Arena fidl_allocator;
        auto node_info = fuchsia_io::wire::NodeInfo::WithService(fidl_allocator);
        completer.Reply(std::move(node_info));
      });

  StartServerThread();

  ASSERT_OK(zxio_create(TakeClientChannel().release(), storage()));

  ASSERT_OK(zxio_close(zxio()));
}

TEST_F(CreateWithOnOpenTest, Service) {
  fidl::Arena fidl_allocator;
  auto node_info = fuchsia_io::wire::NodeInfo::WithService(fidl_allocator);

  SendOnOpenEvent(std::move(node_info));

  ASSERT_OK(zxio_create_with_on_open(TakeClientChannel().release(), storage()));

  StartServerThread();

  ASSERT_OK(zxio_close(zxio()));
}

TEST_F(CreateTest, Tty) {
  zx::eventpair event0, event1;
  ASSERT_OK(zx::eventpair::create(0, &event0, &event1));
  node_server().set_describe_function(
      [event1 = std::move(event1)](DescribeRequestView request,
                                   DescribeCompleter::Sync& completer) mutable {
        fuchsia_io::wire::Tty device = {.event = std::move(event1)};
        fidl::Arena fidl_allocator;
        auto node_info = fuchsia_io::wire::NodeInfo::WithTty(fidl_allocator, std::move(device));
        completer.Reply(std::move(node_info));
      });

  StartServerThread();

  ASSERT_OK(zxio_create(TakeClientChannel().release(), storage()));

  // Closing the zxio object should close our eventpair's peer event.
  zx_signals_t pending = 0;
  ASSERT_EQ(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_NE(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "pending is %u", pending);

  ASSERT_OK(zxio_close(zxio()));

  ASSERT_EQ(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "pending is %u", pending);
}

TEST_F(CreateWithOnOpenTest, Tty) {
  zx::eventpair event0, event1;

  ASSERT_OK(zx::eventpair::create(0, &event0, &event1));
  fuchsia_io::wire::Tty device = {.event = std::move(event1)};
  fidl::Arena fidl_allocator;
  auto node_info = fuchsia_io::wire::NodeInfo::WithTty(fidl_allocator, std::move(device));

  SendOnOpenEvent(std::move(node_info));

  ASSERT_OK(zxio_create_with_on_open(TakeClientChannel().release(), storage()));

  // Closing the zxio object should close our eventpair's peer event.
  zx_signals_t pending = 0;
  ASSERT_EQ(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_NE(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "pending is %u", pending);

  StartServerThread();

  ASSERT_OK(zxio_close(zxio()));

  ASSERT_EQ(event0.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "pending is %u", pending);
}

class TestVmofileServerWithDescribe : public zxio_tests::TestVmofileServer {
 public:
  void set_vmofile(fuchsia_io::wire::Vmofile vmofile) { vmofile_ = std::move(vmofile); }

 protected:
  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) final {
    fidl::Arena fidl_allocator;
    auto node_info = fuchsia_io::wire::NodeInfo::WithVmofile(fidl_allocator, std::move(vmofile_));
    completer.Reply(std::move(node_info));
  }

 private:
  fuchsia_io::wire::Vmofile vmofile_;
};

using CreateVmofileTest = CreateTestBase<TestVmofileServerWithDescribe>;

TEST_F(CreateVmofileTest, File) {
  const uint64_t vmo_size = 5678;
  const uint64_t file_start_offset = 1234;
  const uint64_t file_length = 345;
  const uint64_t offset_within_file = 234;

  node_server().set_seek_offset(offset_within_file);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0u, &vmo));

  fuchsia_io::wire::Vmofile vmofile = {
      .vmo = std::move(vmo),
      .offset = file_start_offset,
      .length = file_length,
  };
  node_server().set_vmofile(std::move(vmofile));

  StartServerThread();

  ASSERT_OK(zxio_create(TakeClientChannel().release(), storage()));

  ASSERT_OK(zxio_close(zxio()));
}

TEST(CreateVmofileWithTypeTest, File) {
  const uint64_t vmo_size = 5678;
  const uint64_t file_start_offset = 1234;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0u, &vmo));

  zx::stream stream;
  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo, file_start_offset, &stream));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create_with_type(&storage, ZXIO_OBJECT_TYPE_VMO, vmo.release(), stream.release()));
  zxio_t* zxio = &storage.io;

  zxio_node_attributes_t attr;
  EXPECT_OK(zxio_attr_get(zxio, &attr));

  EXPECT_TRUE(attr.has.content_size);
  EXPECT_EQ(attr.content_size, vmo_size);

  size_t seek_current_offset = 0u;
  EXPECT_OK(zxio_seek(zxio, ZXIO_SEEK_ORIGIN_CURRENT, 0, &seek_current_offset));
  EXPECT_EQ(static_cast<size_t>(file_start_offset), seek_current_offset);

  ASSERT_OK(zxio_close(zxio));
}

TEST(CreateVmofileWithTypeWrapperTest, File) {
  const uint64_t vmo_size = 5678;
  const uint64_t file_start_offset = 1234;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0u, &vmo));

  zx::stream stream;
  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo, file_start_offset, &stream));

  zxio_storage_t storage;
  ASSERT_OK(zxio::CreateVmo(&storage, std::move(vmo), std::move(stream)));
  zxio_t* zxio = &storage.io;

  zxio_node_attributes_t attr;
  EXPECT_OK(zxio_attr_get(zxio, &attr));

  EXPECT_TRUE(attr.has.content_size);
  EXPECT_EQ(attr.content_size, vmo_size);

  size_t seek_current_offset = 0u;
  EXPECT_OK(zxio_seek(zxio, ZXIO_SEEK_ORIGIN_CURRENT, 0, &seek_current_offset));
  EXPECT_EQ(static_cast<size_t>(file_start_offset), seek_current_offset);

  ASSERT_OK(zxio_close(zxio));
}

using CreateVmofileWithOnOpenTest = CreateTestBase<zxio_tests::TestVmofileServer>;

TEST_F(CreateVmofileWithOnOpenTest, File) {
  const uint64_t vmo_size = 5678;
  const uint64_t file_start_offset = 1234;
  const uint64_t file_length = 345;
  const uint64_t offset_within_file = 234;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0u, &vmo));

  fuchsia_io::wire::Vmofile vmofile = {
      .vmo = std::move(vmo),
      .offset = file_start_offset,
      .length = file_length,
  };
  fidl::Arena fidl_allocator;
  auto node_info = fuchsia_io::wire::NodeInfo::WithVmofile(fidl_allocator, std::move(vmofile));

  SendOnOpenEvent(std::move(node_info));

  node_server().set_seek_offset(offset_within_file);

  StartServerThread();

  ASSERT_OK(zxio_create_with_on_open(TakeClientChannel().release(), storage()));

  // Sanity check the zxio object.
  zxio_node_attributes_t attr;
  EXPECT_OK(zxio_attr_get(zxio(), &attr));

  EXPECT_TRUE(attr.has.content_size);
  EXPECT_EQ(attr.content_size, file_length);

  size_t seek_current_offset = 0u;
  EXPECT_OK(zxio_seek(zxio(), ZXIO_SEEK_ORIGIN_CURRENT, 0, &seek_current_offset));
  EXPECT_EQ(static_cast<size_t>(offset_within_file), seek_current_offset);

  size_t seek_start_offset = 0u;
  EXPECT_OK(zxio_seek(zxio(), ZXIO_SEEK_ORIGIN_START, 0, &seek_start_offset));
  EXPECT_EQ(0, seek_start_offset);

  size_t seek_end_offset = 0u;
  EXPECT_OK(zxio_seek(zxio(), ZXIO_SEEK_ORIGIN_END, 0, &seek_end_offset));
  EXPECT_EQ(static_cast<size_t>(file_length), seek_end_offset);

  ASSERT_OK(zxio_close(zxio()));
}
