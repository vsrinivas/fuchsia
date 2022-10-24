// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.posix.socket/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <future>
#include <latch>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

namespace {

class Server final : public fidl::testing::WireTestBase<fuchsia_posix_socket::StreamSocket> {
 public:
  explicit Server(zx::socket peer) : peer_(std::move(peer)) {}

  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) override {
    ADD_FAILURE("%s should not be called", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Query(QueryCompleter::Sync& completer) final {
    const std::string_view kProtocol = fuchsia_posix_socket::wire::kStreamSocketProtocolName;
    uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
    completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size()));
  }

  void Close(CloseCompleter::Sync& completer) override {
    completer.ReplySuccess();
    completer.Close(ZX_OK);
  }

  void Shutdown(ShutdownRequestView request, ShutdownCompleter::Sync& completer) override {
    shutdown_count_++;
    completer.ReplySuccess();
  }

  void Describe(DescribeCompleter::Sync& completer) override {
    zx::socket peer;
    if (const zx_status_t status =
            peer_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_WRITE, &peer);
        status != ZX_OK) {
      return completer.Close(status);
    }
    fidl::Arena alloc;
    completer.Reply(fuchsia_posix_socket::wire::StreamSocketDescribeResponse::Builder(alloc)
                        .socket(std::move(peer))
                        .Build());
  }

  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override {
    if (on_connect_) {
      on_connect_(peer_, completer);
    } else {
      fidl::testing::WireTestBase<fuchsia_posix_socket::StreamSocket>::Connect(request, completer);
    }
  }

  void GetError(GetErrorCompleter::Sync& completer) override { completer.ReplySuccess(); }

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

  void SetOnConnect(fit::function<void(zx::socket&, ConnectCompleter::Sync&)> cb) {
    on_connect_ = std::move(cb);
  }

  uint16_t ShutdownCount() const { return shutdown_count_.load(); }

 private:
  zx::socket peer_;
  std::atomic<uint16_t> shutdown_count_ = 0;

  fit::function<void(zx::socket&, ConnectCompleter::Sync&)> on_connect_;
};

template <int sock_type>
class BaseTest : public zxtest::Test {
  static_assert(sock_type == ZX_SOCKET_STREAM || sock_type == ZX_SOCKET_DATAGRAM);

 public:
  BaseTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

 protected:
  void SetUp() override {
    zx::socket client_socket;
    ASSERT_OK(zx::socket::create(sock_type, &client_socket, &server_socket_));
    server_.emplace(std::move(client_socket));

    zx::result endpoints = fidl::CreateEndpoints<fuchsia_posix_socket::StreamSocket>();
    ASSERT_OK(endpoints.status_value());

    ASSERT_OK(fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(endpoints->server),
                                           &server_.value()));
    ASSERT_OK(loop_.StartThread("fake-socket-server"));
    ASSERT_OK(
        fdio_fd_create(endpoints->client.channel().release(), client_fd_.reset_and_get_address()));
  }

  const zx::socket& server_socket() { return server_socket_; }

  zx::socket& mutable_server_socket() { return server_socket_; }

  const fbl::unique_fd& client_fd() { return client_fd_; }

  fbl::unique_fd& mutable_client_fd() { return client_fd_; }

  const Server& server() { return server_.value(); }

  Server& mutable_server() { return server_.value(); }

  void set_connected() {
    mutable_server().SetOnConnect(
        [connected = false](zx::socket& peer, Server::ConnectCompleter::Sync& completer) mutable {
          switch (sock_type) {
            case ZX_SOCKET_STREAM:
              if (!connected) {
                connected = true;
                // We need the FDIO to act like it's connected.
                // fdio_internal::stream_socket::kSignalConnected is private, but we know the value.
                EXPECT_OK(peer.signal(0, ZX_USER_SIGNAL_3));
                completer.ReplyError(fuchsia_posix::wire::Errno::kEinprogress);
                break;
              }
              __FALLTHROUGH;
            case ZX_SOCKET_DATAGRAM:
              completer.ReplySuccess();
              break;
          }
        });
    const sockaddr_in addr = {
        .sin_family = AF_INET,
    };
    ASSERT_SUCCESS(
        connect(client_fd().get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)));
  }

  void set_nonblocking_io() {
    int flags;
    ASSERT_GE(flags = fcntl(client_fd().get(), F_GETFL), 0, "%s", strerror(errno));
    ASSERT_SUCCESS(fcntl(client_fd().get(), F_SETFL, flags | O_NONBLOCK));
  }

 private:
  zx::socket clientSocket() {}

  zx::socket server_socket_;
  fbl::unique_fd client_fd_;
  std::optional<Server> server_;
  async::Loop loop_;
};

using TcpSocketTest = BaseTest<ZX_SOCKET_STREAM>;
TEST_F(TcpSocketTest, CloseZXSocketOnTransfer) {
  // A socket's peer is not closed until all copies of that peer are closed. Since the server holds
  // one of those copies (and the file descriptor holds the other), we must destroy the server's
  // copy before asserting that fdio_fd_transfer closes the file descriptor's copy.
  mutable_server().ResetSocket();

  // The file descriptor still holds a copy of the peer; the peer is still open.
  ASSERT_OK(server_socket().wait_one(ZX_SOCKET_WRITABLE, zx::time::infinite_past(), nullptr));

  zx::handle handle;
  ASSERT_OK(fdio_fd_transfer(client_fd().get(), handle.reset_and_get_address()));

  // The file descriptor has been destroyed; the peer is closed.
  ASSERT_OK(server_socket().wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

// Verify scenario, where multi-segment recvmsg is requested, but the socket has
// just enough data to *completely* fill one segment.
// In this scenario, an attempt to read data for the next segment immediately
// fails with ZX_ERR_SHOULD_WAIT, and this may lead to bogus EAGAIN even if some
// data has actually been read.
TEST_F(TcpSocketTest, RecvmsgNonblockBoundary) {
  ASSERT_NO_FATAL_FAILURE(set_connected());
  ASSERT_NO_FATAL_FAILURE(set_nonblocking_io());

  // Write 4 bytes of data to socket.
  size_t actual;
  const uint32_t data_out = 0x12345678;
  EXPECT_OK(server_socket().write(0, &data_out, sizeof(data_out), &actual));
  EXPECT_EQ(actual, sizeof(data_out));

  uint32_t data_in1, data_in2;
  // Fail at compilation stage if anyone changes types.
  // This is mandatory here: we need the first chunk to be exactly the same
  // length as total size of data we just wrote.
  static_assert(sizeof(data_in1) == sizeof(data_out));

  struct iovec iov[] = {
      {
          .iov_base = &data_in1,
          .iov_len = sizeof(data_in1),
      },
      {
          .iov_base = &data_in2,
          .iov_len = sizeof(data_in2),
      },
  };

  struct msghdr msg = {
      .msg_iov = iov,
      .msg_iovlen = std::size(iov),
  };

  EXPECT_EQ(recvmsg(client_fd().get(), &msg, 0), ssize_t(sizeof(data_out)), "%s", strerror(errno));

  EXPECT_SUCCESS(close(mutable_client_fd().release()));
}

// Make sure we can successfully read zero bytes if we pass a zero sized input buffer.
TEST_F(TcpSocketTest, RecvmsgEmptyBuffer) {
  ASSERT_NO_FATAL_FAILURE(set_connected());
  ASSERT_NO_FATAL_FAILURE(set_nonblocking_io());

  // Write 4 bytes of data to socket.
  size_t actual;
  const uint32_t data_out = 0x12345678;
  EXPECT_OK(server_socket().write(0, &data_out, sizeof(data_out), &actual));
  EXPECT_EQ(actual, sizeof(data_out));

  // Try to read into an empty set of io vectors.
  struct msghdr msg = {};

  // We should "successfully" read zero bytes.
  EXPECT_SUCCESS(recvmsg(client_fd().get(), &msg, 0));
}

// Verify scenario, where multi-segment sendmsg is requested, but the socket has
// just enough spare buffer to *completely* read one segment.
// In this scenario, an attempt to send second segment should immediately fail
// with ZX_ERR_SHOULD_WAIT, but the sendmsg should report first segment length
// rather than failing with EAGAIN.
TEST_F(TcpSocketTest, SendmsgNonblockBoundary) {
  ASSERT_NO_FATAL_FAILURE(set_connected());
  ASSERT_NO_FATAL_FAILURE(set_nonblocking_io());

  const size_t memlength = 65536;
  std::unique_ptr<uint8_t[]> memchunk(new uint8_t[memlength]);

  struct iovec iov[] {
    {
        .iov_base = memchunk.get(),
        .iov_len = memlength,
    },
        {
            .iov_base = memchunk.get(),
            .iov_len = memlength,
        },
  };

  const struct msghdr msg = {
      .msg_iov = iov,
      .msg_iovlen = std::size(iov),
  };

  // 1. Fill up the client socket.
  server().FillPeerSocket();

  // 2. Consume one segment of the data
  size_t actual;
  EXPECT_OK(server_socket().read(0, memchunk.get(), memlength, &actual));
  EXPECT_EQ(memlength, actual);

  // 3. Push again 2 packets of <memlength> bytes, observe only one sent.
  EXPECT_EQ(sendmsg(client_fd().get(), &msg, 0), (ssize_t)memlength, "%s", strerror(errno));

  EXPECT_SUCCESS(close(mutable_client_fd().release()));
}

TEST_F(TcpSocketTest, WaitBeginEndConnecting) {
  ASSERT_NO_FATAL_FAILURE(set_nonblocking_io());

  // Like set_connected, but does not advance to the connected state.
  mutable_server().SetOnConnect([](zx::socket& peer, Server::ConnectCompleter::Sync& completer) {
    completer.ReplyError(fuchsia_posix::wire::Errno::kEinprogress);
  });
  const sockaddr_in addr = {
      .sin_family = AF_INET,
  };
  ASSERT_EQ(connect(client_fd().get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), -1);
  ASSERT_ERRNO(EINPROGRESS);

  fdio_t* io = fdio_unsafe_fd_to_io(client_fd().get());
  auto release = fit::defer([io]() { fdio_unsafe_release(io); });

  zx_handle_t handle;

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLIN, &handle, &signals);
    EXPECT_NE(handle, ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_USER_SIGNAL_0 | ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED |
                           ZX_SOCKET_PEER_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLOUT, &handle, &signals);
    EXPECT_NE(handle, ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_USER_SIGNAL_3 | ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLRDHUP, &handle, &signals);
    EXPECT_NE(handle, ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLHUP, &handle, &signals);
    EXPECT_NE(handle, ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_PEER_CLOSED);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_READABLE, &events);
    EXPECT_EQ(int32_t(events), 0);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_PEER_CLOSED, &events);
    EXPECT_EQ(int32_t(events), POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDHUP);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_PEER_WRITE_DISABLED, &events);
    EXPECT_EQ(int32_t(events), POLLIN | POLLRDHUP);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_WRITABLE, &events);
    EXPECT_EQ(int32_t(events), 0);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_WRITE_DISABLED, &events);
    EXPECT_EQ(int32_t(events), POLLOUT | POLLHUP);
  }
}

TEST_F(TcpSocketTest, WaitBeginEndConnected) {
  ASSERT_NO_FATAL_FAILURE(set_connected());
  fdio_t* io = fdio_unsafe_fd_to_io(client_fd().get());
  auto release = fit::defer([io]() { fdio_unsafe_release(io); });

  zx_handle_t handle;

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLIN, &handle, &signals);
    EXPECT_NE(handle, ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLOUT, &handle, &signals);
    EXPECT_NE(handle, ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLRDHUP, &handle, &signals);
    EXPECT_NE(handle, ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLHUP, &handle, &signals);
    EXPECT_NE(handle, ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_PEER_CLOSED);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_READABLE, &events);
    EXPECT_EQ(int32_t(events), POLLIN);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_PEER_CLOSED, &events);
    EXPECT_EQ(int32_t(events), POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDHUP);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_PEER_WRITE_DISABLED, &events);
    EXPECT_EQ(int32_t(events), POLLIN | POLLRDHUP);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_WRITABLE, &events);
    EXPECT_EQ(int32_t(events), POLLOUT);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_WRITE_DISABLED, &events);
    EXPECT_EQ(int32_t(events), POLLOUT | POLLHUP);
  }
}

TEST_F(TcpSocketTest, Shutdown) {
  ASSERT_EQ(shutdown(client_fd().get(), SHUT_RD), 0, "%s", strerror(errno));
  ASSERT_EQ(server().ShutdownCount(), 1);
}

TEST_F(TcpSocketTest, GetReadBufferAvailable) {
  int available = 0;
  EXPECT_EQ(ioctl(client_fd().get(), FIONREAD, &available), 0, "%s", strerror(errno));
  EXPECT_EQ(available, 0);

  constexpr size_t data_size = 47;
  std::array<char, data_size> data_buf;
  size_t actual = 0;
  EXPECT_OK(server_socket().write(0u, data_buf.data(), data_buf.size(), &actual));
  EXPECT_EQ(actual, data_size);

  EXPECT_EQ(ioctl(client_fd().get(), FIONREAD, &available), 0, "%s", strerror(errno));
  EXPECT_EQ(available, data_size);
}

TEST_F(TcpSocketTest, PollNoEvents) {
  ASSERT_NO_FATAL_FAILURE(set_connected());
  struct pollfd pfds[] = {
      {
          .fd = client_fd().get(),
          .events = 0,
      },
  };

  EXPECT_EQ(poll(pfds, std::size(pfds), 5), 0, "error: %s", strerror(errno));
}

using UdpSocketTest = BaseTest<ZX_SOCKET_DATAGRAM>;
TEST_F(UdpSocketTest, DatagramSendMsg) {
  ASSERT_NO_FATAL_FAILURE(set_connected());

  {
    const struct msghdr msg = {};
    // sendmsg should accept 0 length payload.
    EXPECT_SUCCESS(sendmsg(client_fd().get(), &msg, 0));
    // no data will have arrived on the other end.
    constexpr size_t prior = 1337;
    size_t actual = prior;
    std::array<char, 1> rcv_buf;
    EXPECT_EQ(server_socket().read(0, rcv_buf.data(), rcv_buf.size(), &actual), ZX_ERR_SHOULD_WAIT);
    EXPECT_EQ(actual, prior);
  }

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr =
          {
              .s_addr = htonl(INADDR_LOOPBACK),
          },
  };
  const socklen_t addrlen = sizeof(addr);

  constexpr char payload[] = "hello";
  struct iovec iov[] = {
      {
          .iov_base = static_cast<void*>(const_cast<char*>(payload)),
          .iov_len = sizeof(payload),
      },
  };

  struct msghdr msg = {
      .msg_name = &addr,
      .msg_namelen = addrlen,
      .msg_iov = iov,
      .msg_iovlen = std::size(iov),
  };

  EXPECT_EQ(sendmsg(client_fd().get(), &msg, 0), ssize_t(sizeof(payload)), "%s", strerror(errno));

  // sendmsg doesn't fail when msg_namelen is greater than sizeof(struct sockaddr_storage) because
  // what's being tested here is a fuchsia.posix.socket.StreamSocket backed by a
  // zx::socket(ZX_SOCKET_DATAGRAM), a Frankenstein's monster which implements stream semantics on
  // the network and datagram semantics on the transport to the netstack.
  msg.msg_namelen = sizeof(sockaddr_storage) + 1;
  EXPECT_EQ(sendmsg(client_fd().get(), &msg, 0), ssize_t(sizeof(payload)), "%s", strerror(errno));

  {
    size_t actual;
    std::array<char, sizeof(payload) + 1> rcv_buf;
    for (int i = 0; i < 2; i++) {
      EXPECT_OK(server_socket().read(0, rcv_buf.data(), rcv_buf.size(), &actual));
      EXPECT_EQ(actual, sizeof(payload));
    }
  }

  EXPECT_SUCCESS(close(mutable_client_fd().release()));
}

TEST_F(UdpSocketTest, Shutdown) {
  ASSERT_EQ(shutdown(client_fd().get(), SHUT_RD), 0, "%s", strerror(errno));
  ASSERT_EQ(server().ShutdownCount(), 1);
}

class TcpSocketTimeoutTest : public TcpSocketTest {
 protected:
  template <int optname>
  void timeout(fbl::unique_fd& fd, zx::socket& server_socket) {
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
      ASSERT_SUCCESS(setsockopt(fd.get(), SOL_SOCKET, optname, &tv, sizeof(tv)));
      struct timeval actual_tv;
      socklen_t optlen = sizeof(actual_tv);
      ASSERT_EQ(getsockopt(fd.get(), SOL_SOCKET, optname, &actual_tv, &optlen), 0, "%s",
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
        ASSERT_EQ(read(fd.get(), buf, sizeof(buf)), -1);
        break;
      case SO_SNDTIMEO:
        ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), -1);
        break;
    }
    ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK, "%s", strerror(errno));

    const auto elapsed = std::chrono::steady_clock::now() - start;

    // Check that the actual time waited was close to the expectation.
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    const auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);

    // TODO(fxbug.dev/40135): Only the lower bound of the elapsed time is checked. The upper bound
    // check is ignored as the syscall could far miss the defined deadline to return.
    EXPECT_GT(elapsed, timeout - margin, "elapsed=%lld ms (which is not within %lld ms of %lld ms)",
              elapsed_ms.count(), margin.count(), timeout_ms.count());

    // Remove the timeout.
    const struct timeval tv = {};
    ASSERT_SUCCESS(setsockopt(fd.get(), SOL_SOCKET, optname, &tv, sizeof(tv)));
    // Wrap the read/write in a future to enable a timeout. We expect the future
    // to time out.
    std::latch fut_started(1);
    auto fut = std::async(std::launch::async, [&]() -> std::pair<ssize_t, int> {
      fut_started.count_down();

      switch (optname) {
        case SO_RCVTIMEO:
          return std::make_pair(read(fd.get(), buf, sizeof(buf)), errno);
        case SO_SNDTIMEO:
          return std::make_pair(write(fd.get(), buf, sizeof(buf)), errno);
      }
    });
    fut_started.wait();
    EXPECT_EQ(fut.wait_for(margin), std::future_status::timeout);
    // Resetting the remote end socket should cause the read/write to complete.
    server_socket.reset();
    // Closing the socket without returning an error from `getsockopt(_, SO_ERROR, ...)` looks like
    // the connection was gracefully closed. The same behavior is exercised in
    // src/connectivity/network/tests/bsdsocket_test.cc:{StopListenWhileConnect,BlockedIOTest/CloseWhileBlocked}.
    auto return_code_and_errno = fut.get();
    switch (optname) {
      case SO_RCVTIMEO:
        EXPECT_EQ(return_code_and_errno.first, 0);
        break;
      case SO_SNDTIMEO:
        EXPECT_EQ(return_code_and_errno.first, -1);
        ASSERT_EQ(return_code_and_errno.second, EPIPE, "%s",
                  strerror(return_code_and_errno.second));
        break;
    }

    ASSERT_SUCCESS(close(fd.release()));
  }
};

TEST_F(TcpSocketTimeoutTest, Rcv) {
  ASSERT_NO_FATAL_FAILURE(set_connected());
  timeout<SO_RCVTIMEO>(mutable_client_fd(), mutable_server_socket());
}

TEST_F(TcpSocketTimeoutTest, Snd) {
  ASSERT_NO_FATAL_FAILURE(set_connected());
  server().FillPeerSocket();
  timeout<SO_SNDTIMEO>(mutable_client_fd(), mutable_server_socket());
}

}  // namespace
