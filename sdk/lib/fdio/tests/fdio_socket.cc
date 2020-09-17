// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/posix/socket/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-async/cpp/bind.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <future>
#include <latch>

#include <fbl/array.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

class Server final : public llcpp::fuchsia::posix::socket::StreamSocket::Interface {
 public:
  explicit Server(zx::socket peer) : peer_(std::move(peer)) {
    // We need the FDIO to act like it's connected.
    // ZXSIO_SIGNAL_CONNECTED is private, but we know the value.
    ASSERT_OK(peer_.signal(0, ZX_USER_SIGNAL_3));
  }

  void Clone(uint32_t flags, ::zx::channel object, CloneCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync completer) override {
    completer.Reply(ZX_OK);
    completer.Close(ZX_OK);
  }

  void Describe(DescribeCompleter::Sync completer) override {
    llcpp::fuchsia::io::StreamSocket stream_socket;
    zx_status_t status =
        peer_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_WRITE, &stream_socket.socket);
    if (status != ZX_OK) {
      return completer.Close(status);
    }
    llcpp::fuchsia::io::NodeInfo info;
    info.set_stream_socket(fidl::unowned_ptr(&stream_socket));
    completer.Reply(std::move(info));
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

  void Bind(fidl::VectorView<uint8_t> addr, BindCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Bind2(::llcpp::fuchsia::net::SocketAddress addr, Bind2Completer::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Connect(fidl::VectorView<uint8_t> addr, ConnectCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void Connect2(::llcpp::fuchsia::net::SocketAddress addr,
                Connect2Completer::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Listen(int16_t backlog, ListenCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Accept(bool want_addr, AcceptCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Accept2(Accept2Completer::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetSockName(GetSockNameCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetSockName2(GetSockName2Completer::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetPeerName(GetPeerNameCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetPeerName2(GetPeerName2Completer::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void SetSockOpt(int16_t level, int16_t optname, fidl::VectorView<uint8_t> optval,
                  SetSockOptCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetSockOpt(int16_t level, int16_t optname, GetSockOptCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Disconnect(DisconnectCompleter::Sync completer) override {
    return completer.Close(ZX_ERR_NOT_CONNECTED);
  }

  void FillPeerSocket() const {
    zx_info_socket_t info;
    ASSERT_OK(peer_.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
    size_t tx_buf_available = info.tx_buf_max - info.tx_buf_size;
    std::unique_ptr<uint8_t[]> buf(new uint8_t[tx_buf_available + 1]);
    size_t actual;
    ASSERT_OK(peer_.write(0, buf.get(), tx_buf_available, &actual));
    ASSERT_EQ(actual, tx_buf_available);
  }

  void ResetSocket() { peer_.reset(); }

 private:
  zx::socket peer_;
};

template <int sock_type>
class BaseTest : public ::zxtest::Test {
  static_assert(sock_type == ZX_SOCKET_STREAM || sock_type == ZX_SOCKET_DATAGRAM);

 public:
  BaseTest() : server(clientSocket()), loop(&kAsyncLoopConfigNoAttachToCurrentThread) {
    zx::channel client_channel, server_channel;
    ASSERT_OK(zx::channel::create(0, &client_channel, &server_channel));

    ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &server));
    ASSERT_OK(loop.StartThread("fake-socket-server"));
    ASSERT_OK(fdio_fd_create(client_channel.release(), client_fd.reset_and_get_address()));
  }

 protected:
  zx::socket server_socket;
  fbl::unique_fd client_fd;
  Server server;
  async::Loop loop;

 private:
  zx::socket clientSocket() {
    zx::socket client_socket;
    // ASSERT_* macros return on failure; wrap it in a lambda to avoid returning void here.
    [&]() { ASSERT_OK(zx::socket::create(sock_type, &client_socket, &server_socket)); }();
    return client_socket;
  }
};

static void set_nonblocking_io(int fd) {
  int flags;
  EXPECT_GE(flags = fcntl(fd, F_GETFL), 0, "%s", strerror(errno));
  EXPECT_EQ(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0, "%s", strerror(errno));
}

using TcpSocketTest = BaseTest<ZX_SOCKET_STREAM>;
TEST_F(TcpSocketTest, CloseZXSocketOnTransfer) {
  // A socket's peer is not closed until all copies of that peer are closed. Since the server holds
  // one of those copies (and the file descriptor holds the other), we must destroy the server's
  // copy before asserting that fdio_fd_transfer closes the file descriptor's copy.
  server.ResetSocket();

  // The file descriptor still holds a copy of the peer; the peer is still open.
  ASSERT_OK(server_socket.wait_one(ZX_SOCKET_WRITABLE, zx::time::infinite_past(), nullptr));

  zx::handle handle;
  ASSERT_OK(fdio_fd_transfer(client_fd.get(), handle.reset_and_get_address()));

  // The file descriptor has been destroyed; the peer is closed.
  ASSERT_OK(server_socket.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

// Verify scenario, where multi-segment recvmsg is requested, but the socket has
// just enough data to *completely* fill one segment.
// In this scenario, an attempt to read data for the next segment immediately
// fails with ZX_ERR_SHOULD_WAIT, and this may lead to bogus EAGAIN even if some
// data has actually been read.
TEST_F(TcpSocketTest, RecvmsgNonblockBoundary) {
  set_nonblocking_io(client_fd.get());

  // Write 4 bytes of data to socket.
  size_t actual;
  const uint32_t data_out = 0x12345678;
  EXPECT_OK(server_socket.write(0, &data_out, sizeof(data_out), &actual));
  EXPECT_EQ(actual, sizeof(data_out));

  uint32_t data_in1, data_in2;
  // Fail at compilation stage if anyone changes types.
  // This is mandatory here: we need the first chunk to be exactly the same
  // length as total size of data we just wrote.
  static_assert(sizeof(data_in1) == sizeof(data_out));

  struct iovec iov[2];
  iov[0].iov_base = &data_in1;
  iov[0].iov_len = sizeof(data_in1);
  iov[1].iov_base = &data_in2;
  iov[1].iov_len = sizeof(data_in2);

  struct msghdr msg = {};
  msg.msg_iov = iov;
  msg.msg_iovlen = sizeof(iov) / sizeof(*iov);

  EXPECT_EQ(recvmsg(client_fd.get(), &msg, 0), sizeof(data_out), "%s", strerror(errno));

  EXPECT_EQ(close(client_fd.release()), 0, "%s", strerror(errno));
}

// Make sure we can successfully read zero bytes if we pass a zero sized input buffer.
TEST_F(TcpSocketTest, RecvmsgEmptyBuffer) {
  set_nonblocking_io(client_fd.get());

  // Write 4 bytes of data to socket.
  size_t actual;
  const uint32_t data_out = 0x12345678;
  EXPECT_OK(server_socket.write(0, &data_out, sizeof(data_out), &actual));
  EXPECT_EQ(actual, sizeof(data_out));

  // Try to read into an empty set of io vectors.
  struct msghdr msg = {};
  msg.msg_iov = nullptr;
  msg.msg_iovlen = 0;

  // We should "successfully" read zero bytes.
  EXPECT_EQ(recvmsg(client_fd.get(), &msg, 0), 0, "%s", strerror(errno));
}

// Verify scenario, where multi-segment sendmsg is requested, but the socket has
// just enough spare buffer to *completely* read one segment.
// In this scenario, an attempt to send second segment should immediately fail
// with ZX_ERR_SHOULD_WAIT, but the sendmsg should report first segment length
// rather than failing with EAGAIN.
TEST_F(TcpSocketTest, SendmsgNonblockBoundary) {
  set_nonblocking_io(client_fd.get());

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

  // 1. Fill up the client socket.
  server.FillPeerSocket();

  // 2. Consume one segment of the data
  size_t actual;
  EXPECT_OK(server_socket.read(0, memchunk.get(), memlength, &actual));
  EXPECT_EQ(memlength, actual);

  // 3. Push again 2 packets of <memlength> bytes, observe only one sent.
  EXPECT_EQ(sendmsg(client_fd.get(), &msg, 0), (ssize_t)memlength, "%s", strerror(errno));

  EXPECT_EQ(close(client_fd.release()), 0, "%s", strerror(errno));
}

using UdpSocketTest = BaseTest<ZX_SOCKET_DATAGRAM>;
TEST_F(UdpSocketTest, DatagramSendMsg) {
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
  size_t actual = 0;
  // sendmsg should accept 0 length payload.
  EXPECT_EQ(sendmsg(client_fd.get(), &msg, 0), 0, "%s", strerror(errno));
  // no data will have arrived on the other end.
  EXPECT_EQ(server_socket.read(0, rcv_buf, sizeof(rcv_buf), &actual), ZX_ERR_SHOULD_WAIT);
  EXPECT_EQ(actual, 0);

  msg.msg_name = &addr;
  msg.msg_namelen = addrlen;
  msg.msg_iov = iov.data();
  msg.msg_iovlen = iov.size();

  EXPECT_EQ(sendmsg(client_fd.get(), &msg, 0), sizeof(buf), "%s", strerror(errno));

  // sendmsg doesn't fail when msg_namelen is greater than sizeof(struct sockaddr_storage) because
  // what's being tested here is a fuchsia.posix.socket.StreamSocket backed by a
  // zx::socket(ZX_SOCKET_DATAGRAM), a Frankenstein's monster which implements stream semantics on
  // the network and datagram semantics on the transport to the netstack.
  msg.msg_namelen = sizeof(sockaddr_storage) + 1;
  EXPECT_EQ(sendmsg(client_fd.get(), &msg, 0), sizeof(buf), "%s", strerror(errno));

  for (int i = 0; i < 2; i++) {
    EXPECT_OK(server_socket.read(0, rcv_buf, sizeof(rcv_buf), &actual));
    EXPECT_EQ(actual, sizeof(buf));
  }

  EXPECT_EQ(close(client_fd.release()), 0, "%s", strerror(errno));
}

template <int optname>
auto timeout = [](int client_fd, zx::socket server_socket) {
  static_assert(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO);

  // We want this to be a small number so the test is fast, but at least 1
  // second so that we exercise `tv_sec`.
  const auto timeout = std::chrono::seconds(1) + std::chrono::milliseconds(50);
  {
    const auto sec = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const struct timeval tv = {
        .tv_sec = sec.count(),
        .tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(timeout - sec).count(),
    };
    ASSERT_EQ(setsockopt(client_fd, SOL_SOCKET, optname, &tv, sizeof(tv)), 0, "%s",
              strerror(errno));
    struct timeval actual_tv;
    socklen_t optlen = sizeof(actual_tv);
    ASSERT_EQ(getsockopt(client_fd, SOL_SOCKET, optname, &actual_tv, &optlen), 0, "%s",
              strerror(errno));
    ASSERT_EQ(optlen, sizeof(actual_tv));
    ASSERT_EQ(actual_tv.tv_sec, tv.tv_sec);
    ASSERT_EQ(actual_tv.tv_usec, tv.tv_usec);
  }

  const auto margin = std::chrono::milliseconds(50);

  uint8_t buf[16];

  // Perform the read/write. This is the core of the test - we expect the operation to time out
  // per our setting of the timeout above.

  const auto start = std::chrono::steady_clock::now();

  switch (optname) {
    case SO_RCVTIMEO:
      ASSERT_EQ(read(client_fd, buf, sizeof(buf)), -1);
      break;
    case SO_SNDTIMEO:
      ASSERT_EQ(write(client_fd, buf, sizeof(buf)), -1);
      break;
  }
  ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK, "%s", strerror(errno));

  const auto elapsed = std::chrono::steady_clock::now() - start;

  // Check that the actual time waited was close to the expectation.
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

  // TODO(fxbug.dev/40135): Only the lower bound of the elapsed time is checked. The upper bound
  // check is ignored as the syscall could far miss the defined deadline to return.
  EXPECT_GT(elapsed, timeout - margin, "elapsed=%lld ms (which is not within %lld ms of %lld ms)",
            elapsed_ms.count(), margin.count(), std::chrono::milliseconds(timeout).count());

  // Remove the timeout.
  const struct timeval tv = {};
  ASSERT_EQ(setsockopt(client_fd, SOL_SOCKET, optname, &tv, sizeof(tv)), 0, "%s", strerror(errno));
  // Wrap the read/write in a future to enable a timeout. We expect the future
  // to time out.
  std::latch fut_started(1);
  auto fut = std::async(std::launch::async, [&]() -> std::pair<ssize_t, int> {
    fut_started.count_down();

    switch (optname) {
      case SO_RCVTIMEO:
        return std::make_pair(read(client_fd, buf, sizeof(buf)), errno);
      case SO_SNDTIMEO:
        return std::make_pair(write(client_fd, buf, sizeof(buf)), errno);
    }
  });
  fut_started.wait();
  EXPECT_EQ(fut.wait_for(margin), std::future_status::timeout);
  // Resetting the remote end socket should cause the read/write to complete.
  server_socket.reset();
  auto return_code_and_errno = fut.get();
  EXPECT_EQ(return_code_and_errno.first, -1);
  ASSERT_EQ(return_code_and_errno.second, ECONNRESET, "%s", strerror(return_code_and_errno.second));

  ASSERT_EQ(close(client_fd), 0, "%s", strerror(errno));
};

TEST_F(TcpSocketTest, RcvTimeout) {
  timeout<SO_RCVTIMEO>(client_fd.release(), std::move(server_socket));
}

TEST_F(TcpSocketTest, SndTimeout) {
  server.FillPeerSocket();
  timeout<SO_SNDTIMEO>(client_fd.release(), std::move(server_socket));
}

}  // namespace
