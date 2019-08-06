// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/posix/socket/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zxs/protocol.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>

#include <zxtest/zxtest.h>

namespace {

class Server final : public llcpp::fuchsia::posix::socket::Control::Interface {
 public:
  Server(zx::socket peer) : peer_(std::move(peer)) {
    // We need the FDIO to act like it's connected.
    // ZXSIO_SIGNAL_CONNECTED is private, but we know the value.
    ASSERT_OK(peer_.signal(0, ZX_USER_SIGNAL_3));
  }

  void Clone(uint32_t flags, ::zx::channel object, CloneCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync completer) override { return completer.Reply(ZX_OK); }

  void Describe(DescribeCompleter::Sync completer) override {
    llcpp::fuchsia::io::Socket socket;
    zx_status_t status = peer_.duplicate(ZX_RIGHT_SAME_RIGHTS, &socket.socket);
    if (status != ZX_OK) {
      return completer.Close(status);
    }
    llcpp::fuchsia::io::NodeInfo info;
    info.set_socket(std::move(socket));
    return completer.Reply(std::move(info));
  }

  void Sync(SyncCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetAttr(GetAttrCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void SetAttr(uint32_t flags, ::llcpp::fuchsia::io::NodeAttributes attributes,
               SetAttrCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Ioctl(uint32_t opcode, uint64_t max_out, fidl::VectorView<::zx::handle> handles,
             fidl::VectorView<uint8_t> in, IoctlCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Bind(fidl::VectorView<uint8_t> addr, BindCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Connect(fidl::VectorView<uint8_t> addr, ConnectCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Listen(int16_t backlog, ListenCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Accept(int16_t flags, AcceptCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetSockName(GetSockNameCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetPeerName(GetPeerNameCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void SetSockOpt(int16_t level, int16_t optname, fidl::VectorView<uint8_t> optval,
                  SetSockOptCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetSockOpt(int16_t level, int16_t optname, GetSockOptCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void IoctlPOSIX(int16_t req, fidl::VectorView<uint8_t> in,
                  IoctlPOSIXCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  zx::socket peer_;
};

static void set_nonblocking_io(int fd) {
  int flags;
  EXPECT_GE(flags = fcntl(fd, F_GETFL), 0, "%s", strerror(errno));
  EXPECT_EQ(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0, "%s", strerror(errno));
}

TEST(SocketTest, CloseZXSocketOnTransfer) {
  zx::channel client_channel, server_channel;
  ASSERT_OK(zx::channel::create(0, &client_channel, &server_channel));

  zx::socket client_socket, server_socket;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &client_socket, &server_socket));

  int fd;
  {
    // We need a functioning server to create the file descriptor. Since the server retains one end
    // of the socket, we need to destroy the server before asserting that the socket's peer is
    // closed.
    Server server(std::move(client_socket));
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_OK(fidl::Bind(loop.dispatcher(), std::move(server_channel), &server));
    ASSERT_OK(loop.StartThread("fake-socket-server"));

    ASSERT_OK(fdio_fd_create(client_channel.release(), &fd));
  }

  zx_signals_t observed;
  EXPECT_OK(server_socket.wait_one(ZX_SOCKET_WRITABLE, zx::time::infinite_past(), &observed));

  zx_handle_t handle;
  EXPECT_OK(fdio_fd_transfer(fd, &handle));

  EXPECT_OK(server_socket.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), &observed));
  EXPECT_OK(zx_handle_close(handle));
}

// Verify scenario, where multi-segment recvmsg is requested, but the socket has
// just enough data to *completely* fill one segment.
// In this scenario, an attempt to read data for the next segment immediately
// fails with ZX_ERR_SHOULD_WAIT, and this may lead to bogus EAGAIN even if some
// data has actually been read.
TEST(SocketTest, RecvmsgNonblockBoundary) {
  zx::channel client_channel, server_channel;
  ASSERT_OK(zx::channel::create(0, &client_channel, &server_channel));

  zx::socket client_socket, server_socket;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &client_socket, &server_socket));

  Server server(std::move(client_socket));
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  ASSERT_OK(fidl::Bind(loop.dispatcher(), std::move(server_channel), &server));
  ASSERT_OK(loop.StartThread("fake-socket-server"));

  int fd;
  ASSERT_OK(fdio_fd_create(client_channel.release(), &fd));

  set_nonblocking_io(fd);

  // Write 4 bytes of data to socket.
  size_t actual;
  const uint32_t data_out = 0x12345678;
  EXPECT_OK(server_socket.write(0, &data_out, sizeof(data_out), &actual));
  EXPECT_EQ(actual, sizeof(data_out));

  uint32_t data_in1, data_in2;
  // Fail at compilation stage if anyone changes types.
  // This is mandatory here: we need the first chunk to be exactly the same
  // length as total size of data we just wrote.
  assert(sizeof(data_in1) == sizeof(data_out));

  struct iovec iov[2];
  iov[0].iov_base = &data_in1;
  iov[0].iov_len = sizeof(data_in1);
  iov[1].iov_base = &data_in2;
  iov[1].iov_len = sizeof(data_in2);

  struct msghdr msg = {};
  msg.msg_iov = iov;
  msg.msg_iovlen = sizeof(iov) / sizeof(*iov);

  EXPECT_EQ(recvmsg(fd, &msg, 0), sizeof(data_out));

  EXPECT_EQ(close(fd), 0, "%s", strerror(errno));
}

// Verify scenario, where multi-segment sendmsg is requested, but the socket has
// just enough spare buffer to *completely* read one segment.
// In this scenario, an attempt to send second segment should immediately fail
// with ZX_ERR_SHOULD_WAIT, but the sendmsg should report first segment length
// rather than failing with EAGAIN.
TEST(SocketTest, SendmsgNonblockBoundary) {
  zx::channel client_channel, server_channel;
  ASSERT_OK(zx::channel::create(0, &client_channel, &server_channel));

  zx::socket client_socket, server_socket;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &client_socket, &server_socket));

  Server server(std::move(client_socket));
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  ASSERT_OK(fidl::Bind(loop.dispatcher(), std::move(server_channel), &server));
  ASSERT_OK(loop.StartThread("fake-socket-server"));

  int fd;
  ASSERT_OK(fdio_fd_create(client_channel.release(), &fd));

  set_nonblocking_io(fd);

  const size_t memlength = 65536;
  std::unique_ptr<uint8_t[]> memchunk(new uint8_t[memlength]);

  struct iovec iov[2];
  iov[0].iov_base = memchunk.get();
  iov[0].iov_len = memlength;
  iov[1].iov_base = memchunk.get();
  iov[1].iov_len = memlength;

  struct msghdr msg = {};
  msg.msg_iov = iov;
  msg.msg_iovlen = sizeof(iov) / sizeof(*iov);

  // 1. Keep sending data until socket can take no more.
  for (;;) {
    ssize_t count = sendmsg(fd, &msg, 0);
    if (count < 0) {
      EXPECT_EQ(errno, EAGAIN, "%s", strerror(errno));
      break;
    }
    EXPECT_GE(count, 0);
  }

  // 2. Consume one segment of the data
  size_t actual;
  EXPECT_OK(server_socket.read(0, memchunk.get(), memlength, &actual));
  EXPECT_EQ(memlength, actual);

  // 3. Push again 2 packets of <memlength> bytes, observe only one sent.
  EXPECT_EQ((ssize_t)memlength, sendmsg(fd, &msg, 0));

  EXPECT_EQ(close(fd), 0, "%s", strerror(errno));
}

TEST(SocketTest, DatagramSendMsg) {
  zx::channel client_channel, server_channel;
  ASSERT_OK(zx::channel::create(0, &client_channel, &server_channel));

  zx::socket client_socket, server_socket;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &client_socket, &server_socket));

  Server server(std::move(client_socket));
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  ASSERT_OK(fidl::Bind(loop.dispatcher(), std::move(server_channel), &server));
  ASSERT_OK(loop.StartThread("fake-socket-server"));

  int fd;
  ASSERT_OK(fdio_fd_create(client_channel.release(), &fd));

  struct sockaddr_in addr = {};
  socklen_t addrlen = sizeof(addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  const char buf[] = "hello";
  char rcv_buf[4096] = {0};
  std::array<struct iovec, 1> iov = {{{
      .iov_base = (void *)buf,
      .iov_len = sizeof(buf),
  }}};

  struct msghdr msg = {};
  msg.msg_name = &addr;
  msg.msg_namelen = addrlen;
  msg.msg_iov = iov.data();
  msg.msg_iovlen = iov.size();

  EXPECT_EQ(sendmsg(fd, &msg, 0), sizeof(buf), "%s", strerror(errno));

  // Expect sendmsg() to fail when msg_namelen is greater than sizeof(struct sockaddr_storage).
  msg.msg_namelen = sizeof(sockaddr_storage) + 1;
  EXPECT_EQ(sendmsg(fd, &msg, 0), -1);
  EXPECT_EQ(errno, EINVAL, "%s", strerror(errno));

  size_t actual = 0;
  EXPECT_OK(server_socket.read(0, rcv_buf, sizeof(rcv_buf), &actual));
  EXPECT_EQ(actual - FDIO_SOCKET_MSG_HEADER_SIZE, sizeof(buf));
  EXPECT_EQ(close(fd), 0, "%s", strerror(errno));
}

}  // namespace
