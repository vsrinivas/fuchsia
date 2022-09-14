// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Fuchsia's BSD socket tests ensure that fdio and Netstack together produce
// POSIX-like behavior. This module contains tests that exclusively evaluate
// SOCK_STREAM behavior.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>

#include <array>
#include <cerrno>
#include <future>
#include <latch>
#include <variant>

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/connectivity/network/tests/os.h"
#include "util.h"

#if defined(__Fuchsia__)
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/fdio/fd.h>

#include "src/lib/testing/predicates/status.h"
#endif

namespace {

void SetBlocking(int fd, bool blocking) {
  int flags;
  ASSERT_GE(flags = fcntl(fd, F_GETFL), 0) << strerror(errno);
  if (blocking) {
    ASSERT_EQ(flags & O_NONBLOCK, O_NONBLOCK)
        << "got unexpected flags: " << std::hex << std::showbase << flags;
    flags = flags ^ O_NONBLOCK;
  } else {
    ASSERT_EQ(flags & O_NONBLOCK, 0)
        << "got unexpected flags: " << std::hex << std::showbase << flags;
    flags = flags | O_NONBLOCK;
  }
  ASSERT_EQ(fcntl(fd, F_SETFL, flags), 0) << strerror(errno);
}

void AssertExpectedReventsAfterPeerShutdown(int fd) {
  pollfd pfd = {
      .fd = fd,
      // POLLOUT is masked because otherwise the `poll()` will return immediately,
      // before shutdown is complete. POLLWRNORM and POLLRDNORM are masked because
      // we do not yet support them on Fuchsia.
      //
      // TODO(https://fxbug.dev/73258): Support POLLWRNORM and POLLRDNORM on Fuchsia.
      .events =
          std::numeric_limits<decltype(pfd.events)>::max() & ~(POLLOUT | POLLWRNORM | POLLRDNORM),
  };

  int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
  EXPECT_GE(n, 0) << strerror(errno);
  EXPECT_EQ(n, 1);

  EXPECT_EQ(pfd.revents, POLLERR | POLLHUP | POLLRDHUP | POLLIN);
}

class NetStreamSocketsTest : public testing::Test {
 protected:
  void SetUp() override {
    fbl::unique_fd listener;
    ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

    sockaddr_in addr = LoopbackSockaddrV4(0);
    ASSERT_EQ(bind(listener.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);

    socklen_t addrlen = sizeof(addr);
    ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(addr));

    ASSERT_EQ(listen(listener.get(), 0), 0) << strerror(errno);

    ASSERT_TRUE(client_ = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
    ASSERT_EQ(connect(client_.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);

    ASSERT_TRUE(server_ = fbl::unique_fd(accept(listener.get(), nullptr, nullptr)))
        << strerror(errno);
    EXPECT_EQ(close(listener.release()), 0) << strerror(errno);
  }

  fbl::unique_fd& client() { return client_; }

  fbl::unique_fd& server() { return server_; }

 private:
  fbl::unique_fd client_;
  fbl::unique_fd server_;
};

TEST_F(NetStreamSocketsTest, PartialWriteStress) {
  // Generate a payload large enough to fill the client->server buffers.
  std::string big_string;
  {
    size_t tx_capacity;
    ASSERT_NO_FATAL_FAILURE(TxCapacity(client().get(), tx_capacity));

    size_t rx_capacity;
    ASSERT_NO_FATAL_FAILURE(RxCapacity(server().get(), rx_capacity));
    const size_t size = tx_capacity + rx_capacity;
    big_string.reserve(size);
    while (big_string.size() < size) {
      big_string += "Though this upload be but little, it is fierce.";
    }
  }

  {
    // Write in small chunks to allow the outbound TCP to coalesce adjacent writes into a single
    // segment; that is the circumstance in which the data corruption bug that prompted writing
    // this test was observed.
    //
    // Loopback MTU is 64KiB, so use a value smaller than that.
    constexpr size_t write_size = 1 << 10;  // 1 KiB.

    auto s = big_string;
    while (!s.empty()) {
      ssize_t w = write(client().get(), s.data(), std::min(s.size(), write_size));
      ASSERT_GE(w, 0) << strerror(errno);
      s = s.substr(w);
    }
    ASSERT_EQ(shutdown(client().get(), SHUT_WR), 0) << strerror(errno);
  }

  // Read the data and validate it against our payload.
  {
    // Read in small chunks to increase the probability of partial writes from the network
    // endpoint into the zircon socket; that is the circumstance in which the data corruption bug
    // that prompted writing this test was observed.
    //
    // zircon sockets are 256KiB deep, so use a value smaller than that.
    //
    // Note that in spite of the trickery we employ in this test to create the conditions
    // necessary to trigger the data corruption bug, it is still not guaranteed to happen. This is
    // because a race is still necessary to trigger the bug; as netstack is copying bytes from the
    // network to the zircon socket, the application on the other side of this socket (this test)
    // must read between a partial write and the next write.
    constexpr size_t read_size = 1 << 13;  // 8 KiB.

    std::string buf;
    buf.resize(read_size);
    for (size_t i = 0; i < big_string.size();) {
      ssize_t r = read(server().get(), buf.data(), buf.size());
      ASSERT_GT(r, 0) << strerror(errno);

      auto actual = buf.substr(0, r);
      auto expected = big_string.substr(i, r);

      constexpr size_t kChunkSize = 100;
      for (size_t j = 0; j < actual.size(); j += kChunkSize) {
        auto actual_chunk = actual.substr(j, kChunkSize);
        auto expected_chunk = expected.substr(j, actual_chunk.size());
        ASSERT_EQ(actual_chunk, expected_chunk) << "offset " << i + j;
      }
      i += r;
    }
  }
}

TEST_F(NetStreamSocketsTest, PeerClosedPOLLOUT) {
  ASSERT_NO_FATAL_FAILURE(fill_stream_send_buf(server().get(), client().get(), nullptr));

  EXPECT_EQ(close(client().release()), 0) << strerror(errno);

  pollfd pfd = {
      .fd = server().get(),
      .events = POLLOUT,
  };
  int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
  EXPECT_GE(n, 0) << strerror(errno);
  EXPECT_EQ(n, 1);
  EXPECT_EQ(pfd.revents, POLLOUT | POLLERR | POLLHUP);
}

TEST_F(NetStreamSocketsTest, BlockingAcceptWrite) {
  const char msg[] = "hello";
  ASSERT_EQ(write(server().get(), msg, sizeof(msg)), ssize_t(sizeof(msg))) << strerror(errno);
  EXPECT_EQ(close(server().release()), 0) << strerror(errno);

  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(client().get(), buf, sizeof(buf)), ssize_t(sizeof(msg))) << strerror(errno);
  ASSERT_STREQ(buf, msg);
}

TEST_F(NetStreamSocketsTest, SocketAtOOBMark) {
  int result = sockatmark(client().get());
  if (kIsFuchsia) {
    // sockatmark is not supported on Fuchsia.
    EXPECT_EQ(result, -1);
    // TODO(https://fxbug.dev/84632): This should be ENOSYS, not ENOTTY.
    EXPECT_EQ(errno, ENOTTY) << strerror(errno);
  } else {
    EXPECT_EQ(result, 0) << strerror(errno);
  }
}

TEST_F(NetStreamSocketsTest, Sendmmsg) {
  mmsghdr header{
      .msg_hdr = {},
      .msg_len = 0,
  };
  int result = sendmmsg(client().get(), &header, 0u, 0u);
  if (kIsFuchsia) {
    // Fuchsia does not support sendmmsg().
    // TODO(https://fxbug.dev/45262, https://fxbug.dev/42678): Implement sendmmsg().
    EXPECT_EQ(result, -1);
    EXPECT_EQ(errno, ENOSYS) << strerror(errno);
  } else {
    EXPECT_EQ(result, 0) << strerror(errno);
  }
}

TEST_F(NetStreamSocketsTest, Recvmmsg) {
  mmsghdr header{
      .msg_hdr = {},
      .msg_len = 0,
  };
  int result = recvmmsg(client().get(), &header, 1u, MSG_DONTWAIT, nullptr);
  EXPECT_EQ(result, -1);
  if (kIsFuchsia) {
    // Fuchsia does not support recvmmsg().
    // TODO(https://fxbug.dev/45260): Implement recvmmsg().
    EXPECT_EQ(errno, ENOSYS) << strerror(errno);
  } else {
    EXPECT_EQ(errno, EAGAIN) << strerror(errno);
  }
}

TEST_F(NetStreamSocketsTest, BlockingAcceptDupWrite) {
  fbl::unique_fd dupfd;
  ASSERT_TRUE(dupfd = fbl::unique_fd(dup(server().get()))) << strerror(errno);
  EXPECT_EQ(close(server().release()), 0) << strerror(errno);

  const char msg[] = "hello";
  ASSERT_EQ(write(dupfd.get(), msg, sizeof(msg)), ssize_t(sizeof(msg))) << strerror(errno);
  EXPECT_EQ(close(dupfd.release()), 0) << strerror(errno);

  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(client().get(), buf, sizeof(buf)), ssize_t(sizeof(msg))) << strerror(errno);
  ASSERT_STREQ(buf, msg);
}

TEST_F(NetStreamSocketsTest, Shutdown) {
  EXPECT_EQ(shutdown(server().get(), SHUT_WR), 0) << strerror(errno);

  pollfd pfd = {
      .fd = client().get(),
      .events = POLLRDHUP,
  };
  int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
  EXPECT_GE(n, 0) << strerror(errno);
  EXPECT_EQ(n, 1);
  EXPECT_EQ(pfd.revents, POLLRDHUP);
}

TEST_F(NetStreamSocketsTest, ResetOnFullReceiveBufferShutdown) {
  // Fill the receive buffer of the client socket.
  ASSERT_NO_FATAL_FAILURE(fill_stream_send_buf(server().get(), client().get(), nullptr));

  // Setting SO_LINGER to 0 and `close`ing the server socket should
  // immediately send a TCP RST.
  linger opt = {
      .l_onoff = 1,
      .l_linger = 0,
  };
  EXPECT_EQ(setsockopt(server().get(), SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)), 0)
      << strerror(errno);

  // Close the server to trigger a TCP RST now that linger is 0.
  EXPECT_EQ(close(server().release()), 0) << strerror(errno);

  // Wait for the RST.
  pollfd pfd = {
      .fd = client().get(),
  };
  int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);
  EXPECT_EQ(pfd.revents, POLLHUP | POLLERR);

  // The socket is no longer connected.
  EXPECT_EQ(shutdown(client().get(), SHUT_RD), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);

  // Create another socket to ensure that the networking stack hasn't panicked.
  fbl::unique_fd test_sock;
  ASSERT_TRUE(test_sock = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
}

// Tests that a socket which has completed SHUT_RDWR responds to incoming data with RST.
TEST_F(NetStreamSocketsTest, ShutdownReset) {
  // This test is tricky. In Linux we could shutdown(SHUT_RDWR) the server socket, write() some
  // data on the client socket, and observe the server reply with RST. The SHUT_WR would move the
  // server socket state out of ESTABLISHED (to FIN-WAIT2 after sending FIN and receiving an ACK)
  // and SHUT_RD would close the receiver. Only when the server socket has transitioned out of
  // ESTABLISHED state. At this point, the server socket would respond to incoming data with RST.
  //
  // In Fuchsia this is more complicated because each socket is a distributed system (consisting
  // of netstack and fdio) wherein the socket state is eventually consistent. We must take care to
  // synchronize our actions with netstack's state as we're testing that netstack correctly sends
  // a RST in response to data received after shutdown(SHUT_RDWR).
  //
  // We can manipulate and inspect state using only shutdown() and poll(), both of which operate
  // on fdio state rather than netstack state. Combined with the fact that SHUT_RD is not
  // observable by the peer (i.e. doesn't cause any network traffic), means we are in a pickle.
  //
  // On the other hand, SHUT_WR does cause a FIN to be sent, which can be observed by the peer
  // using poll(POLLRDHUP). Note also that netstack observes SHUT_RD and SHUT_WR on different
  // threads, meaning that a race condition still exists. At the time of writing, this is the best
  // we can do.

  // Change internal state to disallow further reads and writes. The state change propagates to
  // netstack at some future time. We have no way to observe that SHUT_RD has propagated (because
  // it propagates independently from SHUT_WR).
  ASSERT_EQ(shutdown(server().get(), SHUT_RDWR), 0) << strerror(errno);

  // Wait for the FIN to arrive at the client and for the state to propagate to the client's fdio.
  {
    pollfd pfd = {
        .fd = client().get(),
        .events = POLLRDHUP,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(pfd.revents, POLLRDHUP);
  }

  // Send data from the client(). The server should now very likely be in SHUT_RD and respond with
  // RST.
  char c;
  ASSERT_EQ(write(client().get(), &c, sizeof(c)), ssize_t(sizeof(c))) << strerror(errno);

  // Wait for the client to receive the RST and for the state to propagate through its fdio.
  pollfd pfd = {
      .fd = client().get(),
  };
  int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);
  EXPECT_EQ(pfd.revents, POLLHUP | POLLERR);
}

// ShutdownPendingWrite tests for all of the application writes that
// occurred before shutdown SHUT_WR, to be received by the remote.
TEST_F(NetStreamSocketsTest, ShutdownPendingWrite) {
  // Fill the send buffer of the server socket so that we have some
  // pending data waiting to be sent out to the remote.
  ssize_t wrote;
  ASSERT_NO_FATAL_FAILURE(fill_stream_send_buf(server().get(), client().get(), &wrote));

  // SHUT_WR should enqueue a FIN after all of the application writes.
  EXPECT_EQ(shutdown(server().get(), SHUT_WR), 0) << strerror(errno);

  // All client reads are expected to return here, including the last
  // read on receiving a FIN. Keeping a timeout for unexpected failures.
  timeval tv = {
      .tv_sec = std::chrono::seconds(kTimeout).count(),
  };
  EXPECT_EQ(setsockopt(client().get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0)
      << strerror(errno);

  ssize_t rcvd = 0;
  // Keep a large enough buffer to reduce the number of read calls, as
  // we expect the receive buffer to be filled up at this point.
  char buf[4096];
  // Each read would make room for the server to send out more data
  // that has been enqueued from successful server socket writes.
  for (;;) {
    ssize_t ret = read(client().get(), &buf, sizeof(buf));
    ASSERT_GE(ret, 0) << strerror(errno);
    // Expect the last read to return 0 after the stack sees a FIN.
    if (ret == 0) {
      break;
    }
    rcvd += ret;
  }
  // Expect no data drops and all written data by server is received
  // by the client().
  EXPECT_EQ(rcvd, wrote);
}

// Test close/shutdown of listening socket with multiple non-blocking connects.
// This tests client sockets in connected and connecting states.
void TestListenWhileConnect(const IOMethod& io_method, void (*stopListen)(fbl::unique_fd&)) {
  fbl::unique_fd listener;
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  // This test is only interested in deterministically getting a socket in
  // connecting state. For that, we use a listen backlog of zero which would
  // mean there is exactly one connection that gets established and is enqueued
  // to the accept queue. We poll on the listener to ensure that is enqueued.
  // After that the subsequent client connect will stay in connecting state as
  // the accept queue is full.
  ASSERT_EQ(listen(listener.get(), 0), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  fbl::unique_fd established_client;
  ASSERT_TRUE(established_client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0)))
      << strerror(errno);
  ASSERT_EQ(connect(established_client.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  // Ensure that the accept queue has the completed connection.
  {
    pollfd pfd = {
        .fd = listener.get(),
        .events = POLLIN,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(pfd.revents, POLLIN);
  }

  fbl::unique_fd connecting_client;
  ASSERT_TRUE(connecting_client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);
  EXPECT_EQ(connect(connecting_client.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), -1);
  EXPECT_EQ(errno, EINPROGRESS) << strerror(errno);

  ASSERT_NO_FATAL_FAILURE(stopListen(listener));

  std::array<std::pair<int, int>, 2> sockets = {
      std::make_pair(established_client.get(), ECONNRESET),
      std::make_pair(connecting_client.get(), ECONNREFUSED),
  };
  for (size_t i = 0; i < sockets.size(); i++) {
    SCOPED_TRACE("i=" + std::to_string(i));
    auto [fd, expected_errno] = sockets[i];
    ASSERT_NO_FATAL_FAILURE(AssertExpectedReventsAfterPeerShutdown(fd));

    char c;
    EXPECT_EQ(io_method.ExecuteIO(fd, &c, sizeof(c)), -1);
    EXPECT_EQ(errno, expected_errno) << strerror(errno) << " vs " << strerror(expected_errno);

    {
      // The error should have been consumed.
      int err;
      socklen_t optlen = sizeof(err);
      ASSERT_EQ(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
      ASSERT_EQ(optlen, sizeof(err));
      ASSERT_EQ(err, 0) << strerror(err);
    }

    bool is_write = io_method.isWrite();
    auto undo = DisableSigPipe(is_write);

    if (is_write) {
      ASSERT_EQ(io_method.ExecuteIO(fd, &c, sizeof(c)), -1);
      EXPECT_EQ(errno, EPIPE) << strerror(errno);

      // The error should have been consumed.
      int err;
      socklen_t optlen = sizeof(err);
      ASSERT_EQ(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
      ASSERT_EQ(optlen, sizeof(err));
      ASSERT_EQ(err, 0) << strerror(err);
    } else {
      ASSERT_EQ(io_method.ExecuteIO(fd, &c, sizeof(c)), 0) << strerror(errno);
    }
  }
}

class StopListenWhileConnect : public testing::TestWithParam<IOMethod> {};

TEST_P(StopListenWhileConnect, Close) {
  TestListenWhileConnect(
      GetParam(), [](fbl::unique_fd& f) { EXPECT_EQ(close(f.release()), 0) << strerror(errno); });
}

TEST_P(StopListenWhileConnect, Shutdown) {
  TestListenWhileConnect(GetParam(), [](fbl::unique_fd& f) {
    ASSERT_EQ(shutdown(f.get(), SHUT_RD), 0) << strerror(errno);
  });
}

INSTANTIATE_TEST_SUITE_P(NetStreamTest, StopListenWhileConnect, testing::ValuesIn(kAllIOMethods),
                         [](const testing::TestParamInfo<IOMethod>& info) {
                           return info.param.IOMethodToString();
                         });

using ConnectingIOParams = std::tuple<IOMethod, bool>;

class ConnectingIOTest : public testing::TestWithParam<ConnectingIOParams> {};

// Tests the application behavior when we start to read and write from a stream socket that is not
// yet connected.
TEST_P(ConnectingIOTest, BlockedIO) {
  auto const& [io_method, close_listener] = GetParam();
  fbl::unique_fd listener;
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  ASSERT_EQ(listen(listener.get(), 0), 0) << strerror(errno);

  // Setup a test client connection over which we test socket reads
  // when the connection is not yet established.

  // Linux default behavior is to complete one more connection than what
  // was passed as listen backlog (zero here).
  // Hence we initiate 2 client connections in this order:
  // (1) a precursor client for the sole purpose of filling up the server
  //     accept queue after handshake completion.
  // (2) a test client that keeps trying to establish connection with
  //     server, but remains in SYN-SENT.
  fbl::unique_fd precursor_client;
  ASSERT_TRUE(precursor_client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0)))
      << strerror(errno);
  ASSERT_EQ(connect(precursor_client.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  // Observe the precursor client connection on the server side. This ensures that the TCP stack's
  // server accept queue is updated with the precursor client connection before any subsequent
  // client connect requests. The precursor client connect call returns after handshake
  // completion, but not necessarily after the server side has processed the ACK from the client
  // and updated its accept queue.
  {
    pollfd pfd = {
        .fd = listener.get(),
        .events = POLLIN,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(pfd.revents, POLLIN);
  }

  // The test client connection would get established _only_ after both
  // these conditions are met:
  // (1) prior client connections are accepted by the server thus
  //     making room for a new connection.
  // (2) the server-side TCP stack completes handshake in response to
  //     the retransmitted SYN for the test client connection.
  //
  // The test would likely perform socket reads before any connection
  // timeout.
  fbl::unique_fd test_client;
  ASSERT_TRUE(test_client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  // Sample data to be written.
  char sample_data[] = "Sample Data";
  // To correctly test reads, keep alteast one byte larger read buffer than what would be written.
  char recvbuf[sizeof(sample_data) + 1] = {};
  bool is_write = io_method.isWrite();
  auto ExecuteIO = [&, op = io_method]() {
    if (is_write) {
      return op.ExecuteIO(test_client.get(), sample_data, sizeof(sample_data));
    }
    return op.ExecuteIO(test_client.get(), recvbuf, sizeof(recvbuf));
  };
  auto undo = DisableSigPipe(is_write);

  EXPECT_EQ(ExecuteIO(), -1);
  if (is_write) {
    EXPECT_EQ(errno, EPIPE) << strerror(errno);
  } else {
    EXPECT_EQ(errno, ENOTCONN) << strerror(errno);
  }

  ASSERT_EQ(connect(test_client.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), -1);
  ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

  // Test socket I/O without waiting for connection to be established.
  EXPECT_EQ(ExecuteIO(), -1);
  EXPECT_EQ(errno, EWOULDBLOCK) << strerror(errno);

  std::latch fut_started(1);
  // Asynchronously block on I/O from the test client socket.
  const auto fut = std::async(std::launch::async, [&, err = close_listener]() {
    // Make the socket blocking.
    ASSERT_NO_FATAL_FAILURE(SetBlocking(test_client.get(), true));

    fut_started.count_down();

    if (err) {
      EXPECT_EQ(ExecuteIO(), -1);
      EXPECT_EQ(errno, ECONNREFUSED) << strerror(errno);
    } else {
      EXPECT_EQ(ExecuteIO(), ssize_t(sizeof(sample_data))) << strerror(errno);
    }
  });
  fut_started.wait();
  ASSERT_NO_FATAL_FAILURE(AssertBlocked(fut));

  if (close_listener) {
    EXPECT_EQ(close(listener.release()), 0) << strerror(errno);
  } else {
    // Accept the precursor connection to make room for the test client
    // connection to complete.
    fbl::unique_fd precursor_accept;
    ASSERT_TRUE(precursor_accept = fbl::unique_fd(accept(listener.get(), nullptr, nullptr)))
        << strerror(errno);
    EXPECT_EQ(close(precursor_accept.release()), 0) << strerror(errno);
    EXPECT_EQ(close(precursor_client.release()), 0) << strerror(errno);

    // Accept the test client connection.
    fbl::unique_fd test_accept;
    ASSERT_TRUE(test_accept =
                    fbl::unique_fd(accept4(listener.get(), nullptr, nullptr, SOCK_NONBLOCK)))
        << strerror(errno);

    if (is_write) {
      // Ensure that we read the data whose send request was enqueued until
      // the connection was established.

      // TODO(https://fxbug.dev/67928): Replace these multiple non-blocking
      // reads with a single blocking read after Fuchsia supports atomic
      // vectorized writes.
      size_t total = 0;
      while (total < sizeof(sample_data)) {
        pollfd pfd = {
            .fd = test_accept.get(),
            .events = POLLIN,
        };
        int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
        ASSERT_GE(n, 0) << strerror(errno);
        ASSERT_EQ(n, 1);
        ASSERT_EQ(pfd.revents, POLLIN);
        ssize_t res = read(test_accept.get(), recvbuf + total, sizeof(recvbuf) - total);
        ASSERT_GE(res, 0) << strerror(errno);
        total += res;
      }
      ASSERT_EQ(total, sizeof(sample_data));
      ASSERT_STREQ(recvbuf, sample_data);
    } else {
      // Write data to unblock the socket read on the test client connection.
      ASSERT_EQ(write(test_accept.get(), sample_data, sizeof(sample_data)),
                ssize_t(sizeof(sample_data)))
          << strerror(errno);
    }
  }

  EXPECT_EQ(fut.wait_for(kTimeout), std::future_status::ready);
}

std::string ConnectingIOParamsToString(const testing::TestParamInfo<ConnectingIOParams> info) {
  auto const& [io_method, close_listener] = info.param;
  std::stringstream s;
  if (close_listener) {
    s << "CloseListener";
  } else {
    s << "Accept";
  }
  s << "During" << io_method.IOMethodToString();

  return s.str();
}

INSTANTIATE_TEST_SUITE_P(NetStreamTest, ConnectingIOTest,
                         testing::Combine(testing::ValuesIn(kAllIOMethods),
                                          testing::Values(false, true)),
                         ConnectingIOParamsToString);

class TimeoutSockoptsTest : public testing::TestWithParam<int /* optname */> {};

TEST_P(TimeoutSockoptsTest, TimeoutSockopts) {
  int optname = GetParam();
  ASSERT_TRUE(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO);

  fbl::unique_fd socket_fd;
  ASSERT_TRUE(socket_fd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  // Set the timeout.
  const timeval expected_tv = {
      .tv_sec = 39,
      // NB: for some reason, Linux's resolution is limited to 4ms.
      .tv_usec = 504000,
  };
  EXPECT_EQ(setsockopt(socket_fd.get(), SOL_SOCKET, optname, &expected_tv, sizeof(expected_tv)), 0)
      << strerror(errno);

  // Reading it back should work.
  {
    timeval actual_tv;
    socklen_t optlen = sizeof(actual_tv);
    EXPECT_EQ(getsockopt(socket_fd.get(), SOL_SOCKET, optname, &actual_tv, &optlen), 0)
        << strerror(errno);
    EXPECT_EQ(optlen, sizeof(actual_tv));
    EXPECT_EQ(actual_tv.tv_sec, expected_tv.tv_sec);
    EXPECT_EQ(actual_tv.tv_usec, expected_tv.tv_usec);
  }

  // Reading it back with too much space should work and set optlen.
  {
    struct {
      timeval tv;
      char unused;
    } actual_tv_with_extra = {
        .unused = 0x44,
    };
    socklen_t optlen = sizeof(actual_tv_with_extra);
    EXPECT_EQ(getsockopt(socket_fd.get(), SOL_SOCKET, optname, &actual_tv_with_extra, &optlen), 0)
        << strerror(errno);
    EXPECT_EQ(optlen, sizeof(timeval));
    EXPECT_EQ(actual_tv_with_extra.tv.tv_sec, expected_tv.tv_sec);
    EXPECT_EQ(actual_tv_with_extra.tv.tv_usec, expected_tv.tv_usec);
    EXPECT_EQ(actual_tv_with_extra.unused, 0x44);
  }

  // Reading it back without enough space should fail gracefully.
  {
    constexpr char kGarbage = 0x44;
    timeval actual_tv;
    memset(&actual_tv, kGarbage, sizeof(actual_tv));
    constexpr socklen_t too_small = sizeof(actual_tv) - 7;
    static_assert(too_small > 0);
    socklen_t optlen = too_small;
    // TODO: Decide if we want to match Linux's behaviour. It writes to only
    // the first optlen bytes of the timeval.
    int ret = getsockopt(socket_fd.get(), SOL_SOCKET, optname, &actual_tv, &optlen);
    if (kIsFuchsia) {
      EXPECT_EQ(ret, -1);
      EXPECT_EQ(errno, EINVAL) << strerror(errno);
    } else {
      EXPECT_EQ(ret, 0) << strerror(errno);
      EXPECT_EQ(optlen, too_small);
      EXPECT_EQ(memcmp(&actual_tv, &expected_tv, too_small), 0);
      const char* tv = reinterpret_cast<char*>(&actual_tv);
      for (size_t i = too_small; i < sizeof(actual_tv); i++) {
        EXPECT_EQ(tv[i], kGarbage);
      }
    }
  }

  // Setting it without enough space should fail gracefully.
  EXPECT_EQ(setsockopt(socket_fd.get(), SOL_SOCKET, optname, &expected_tv, sizeof(expected_tv) - 1),
            -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);

  // Setting it with too much space should work okay.
  {
    const timeval expected_tv2 = {
        .tv_sec = 42,
        .tv_usec = 0,
    };
    socklen_t optlen = sizeof(expected_tv2) + 1;  // Too big.
    EXPECT_EQ(setsockopt(socket_fd.get(), SOL_SOCKET, optname, &expected_tv2, optlen), 0)
        << strerror(errno);

    timeval actual_tv;
    EXPECT_EQ(getsockopt(socket_fd.get(), SOL_SOCKET, optname, &actual_tv, &optlen), 0)
        << strerror(errno);
    EXPECT_EQ(optlen, sizeof(expected_tv2));
    EXPECT_EQ(actual_tv.tv_sec, expected_tv2.tv_sec);
    EXPECT_EQ(actual_tv.tv_usec, expected_tv2.tv_usec);
  }

  // Disabling rcvtimeo by setting it to zero should work.
  const timeval zero_tv = {
      .tv_sec = 0,
      .tv_usec = 0,
  };
  EXPECT_EQ(setsockopt(socket_fd.get(), SOL_SOCKET, optname, &zero_tv, sizeof(zero_tv)), 0)
      << strerror(errno);

  // Reading back the disabled timeout should work.
  {
    timeval actual_tv;
    memset(&actual_tv, 55, sizeof(actual_tv));
    socklen_t optlen = sizeof(actual_tv);
    EXPECT_EQ(getsockopt(socket_fd.get(), SOL_SOCKET, optname, &actual_tv, &optlen), 0)
        << strerror(errno);
    EXPECT_EQ(optlen, sizeof(actual_tv));
    EXPECT_EQ(actual_tv.tv_sec, zero_tv.tv_sec);
    EXPECT_EQ(actual_tv.tv_usec, zero_tv.tv_usec);
  }
}

INSTANTIATE_TEST_SUITE_P(NetStreamTest, TimeoutSockoptsTest,
                         testing::Values(SO_RCVTIMEO, SO_SNDTIMEO));

const int32_t kConnections = 100;

TEST(NetStreamTest, BlockingAcceptWriteMultiple) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(acptfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), kConnections), 0) << strerror(errno);

  fbl::unique_fd clientfds[kConnections];
  for (auto& clientfd : clientfds) {
    ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
    ASSERT_EQ(connect(clientfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);
  }

  const char msg[] = "hello";
  for (int i = 0; i < kConnections; i++) {
    fbl::unique_fd connfd;
    ASSERT_TRUE(connfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);

    ASSERT_EQ(write(connfd.get(), msg, sizeof(msg)), ssize_t(sizeof(msg))) << strerror(errno);
    EXPECT_EQ(close(connfd.release()), 0) << strerror(errno);
  }

  for (auto& clientfd : clientfds) {
    char buf[sizeof(msg) + 1] = {};
    ASSERT_EQ(read(clientfd.get(), buf, sizeof(buf)), ssize_t(sizeof(msg))) << strerror(errno);
    ASSERT_STREQ(buf, msg);
    EXPECT_EQ(close(clientfd.release()), 0) << strerror(errno);
  }

  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingAcceptWrite) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(acptfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), 0), 0) << strerror(errno);

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  pollfd pfd = {
      .fd = acptfd.get(),
      .events = POLLIN,
  };
  int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);

  const char msg[] = "hello";
  ASSERT_EQ(write(connfd.get(), msg, sizeof(msg)), ssize_t(sizeof(msg))) << strerror(errno);
  EXPECT_EQ(close(connfd.release()), 0) << strerror(errno);

  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(clientfd.get(), buf, sizeof(buf)), ssize_t(sizeof(msg))) << strerror(errno);
  ASSERT_STREQ(buf, msg);
  EXPECT_EQ(close(clientfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingAcceptDupWrite) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(acptfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), 0), 0) << strerror(errno);

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  pollfd pfd = {
      .fd = acptfd.get(),
      .events = POLLIN,
  };
  int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);

  fbl::unique_fd dupfd;
  ASSERT_TRUE(dupfd = fbl::unique_fd(dup(connfd.get()))) << strerror(errno);
  EXPECT_EQ(close(connfd.release()), 0) << strerror(errno);

  const char msg[] = "hello";
  ASSERT_EQ(write(dupfd.get(), msg, sizeof(msg)), ssize_t(sizeof(msg))) << strerror(errno);
  EXPECT_EQ(close(dupfd.release()), 0) << strerror(errno);

  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(clientfd.get(), buf, sizeof(buf)), ssize_t(sizeof(msg))) << strerror(errno);
  ASSERT_STREQ(buf, msg);
  EXPECT_EQ(close(clientfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingConnectWrite) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(acptfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), 0), 0) << strerror(errno);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  int ret;
  EXPECT_EQ(ret = connect(connfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
            -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

    pollfd pfd = {
        .fd = connfd.get(),
        .events = POLLOUT,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);

    int err;
    socklen_t optlen = sizeof(err);
    ASSERT_EQ(getsockopt(connfd.get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
    ASSERT_EQ(optlen, sizeof(err));
    ASSERT_EQ(err, 0) << strerror(err);
  }

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);

  const char msg[] = "hello";
  ASSERT_EQ(write(connfd.get(), msg, sizeof(msg)), ssize_t(sizeof(msg))) << strerror(errno);
  EXPECT_EQ(close(connfd.release()), 0) << strerror(errno);

  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(clientfd.get(), buf, sizeof(buf)), ssize_t(sizeof(msg))) << strerror(errno);
  ASSERT_STREQ(buf, msg);
  EXPECT_EQ(close(clientfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingConnectRead) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(acptfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), 0), 0) << strerror(errno);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  int ret;
  EXPECT_EQ(ret = connect(connfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
            -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

    fbl::unique_fd clientfd;
    ASSERT_TRUE(clientfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr)))
        << strerror(errno);

    const char msg[] = "hello";
    ASSERT_EQ(write(clientfd.get(), msg, sizeof(msg)), ssize_t(sizeof(msg))) << strerror(errno);
    EXPECT_EQ(close(clientfd.release()), 0) << strerror(errno);

    // Note: the success of connection can be detected with POLLOUT, but
    // we use POLLIN here to wait until some data is written by the peer.
    pollfd pfd = {
        .fd = connfd.get(),
        .events = POLLIN,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);

    int err;
    socklen_t optlen = sizeof(err);
    ASSERT_EQ(getsockopt(connfd.get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
    ASSERT_EQ(optlen, sizeof(err));
    ASSERT_EQ(err, 0) << strerror(err);

    char buf[sizeof(msg) + 1] = {};
    ASSERT_EQ(read(connfd.get(), buf, sizeof(buf)), ssize_t(sizeof(msg))) << strerror(errno);
    ASSERT_STREQ(buf, msg);
    EXPECT_EQ(close(connfd.release()), 0) << strerror(errno);
    EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
  }
}

TEST(NetStreamTest, NonBlockingConnectRefused) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(acptfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);

  // No listen() on acptfd.

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  int ret;
  EXPECT_EQ(ret = connect(connfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
            -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

    pollfd pfd = {
        .fd = connfd.get(),
        .events = POLLOUT,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);

    int err;
    socklen_t optlen = sizeof(err);
    ASSERT_EQ(getsockopt(connfd.get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
    ASSERT_EQ(optlen, sizeof(err));
    ASSERT_EQ(err, ECONNREFUSED) << strerror(err);
  }

  EXPECT_EQ(close(connfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, GetTcpInfo) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  {
    tcp_info info;
    socklen_t info_len = sizeof(tcp_info);
    ASSERT_EQ(getsockopt(fd.get(), SOL_TCP, TCP_INFO, &info, &info_len), 0) << strerror(errno);
    ASSERT_EQ(sizeof(tcp_info), info_len);

    if (kIsFuchsia) {
      // Unsupported fields are intentionally initialized with garbage for explicitness.
      constexpr int kGarbage = 0xff;
      uint32_t initialization;
      memset(&initialization, kGarbage, sizeof(initialization));

      ASSERT_NE(info.tcpi_state, initialization);
      ASSERT_NE(info.tcpi_ca_state, initialization);
      ASSERT_NE(info.tcpi_rto, initialization);
      ASSERT_NE(info.tcpi_rtt, initialization);
      ASSERT_NE(info.tcpi_rttvar, initialization);
      ASSERT_NE(info.tcpi_snd_ssthresh, initialization);
      ASSERT_NE(info.tcpi_snd_cwnd, initialization);
// TODO(https://fxbug.dev/64200): our Linux sysroot is too old to know about this field.
#if defined(__Fuchsia__)
      ASSERT_NE(info.tcpi_reord_seen, initialization);
#endif

      tcp_info expected;
      memset(&expected, kGarbage, sizeof(expected));
      expected.tcpi_state = info.tcpi_state;
      expected.tcpi_ca_state = info.tcpi_ca_state;
      expected.tcpi_rto = info.tcpi_rto;
      expected.tcpi_rtt = info.tcpi_rtt;
      expected.tcpi_rttvar = info.tcpi_rttvar;
      expected.tcpi_snd_ssthresh = info.tcpi_snd_ssthresh;
      expected.tcpi_snd_cwnd = info.tcpi_snd_cwnd;
// TODO(https://fxbug.dev/64200): our Linux sysroot is too old to know about this field.
#if defined(__Fuchsia__)
      expected.tcpi_reord_seen = info.tcpi_reord_seen;
#endif

      ASSERT_EQ(memcmp(&info, &expected, sizeof(tcp_info)), 0);
    }
  }

  // Test that we can partially retrieve TCP_INFO.
  {
    uint8_t tcpi_state;
    socklen_t info_len = sizeof(tcpi_state);
    ASSERT_EQ(getsockopt(fd.get(), SOL_TCP, TCP_INFO, &tcpi_state, &info_len), 0)
        << strerror(errno);
    ASSERT_EQ(info_len, sizeof(tcpi_state));
  }

  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, GetSocketAcceptConn) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  auto assert_so_accept_conn_eq = [&fd](int expected) {
    int got = ~expected;
    socklen_t got_len = sizeof(got);
    ASSERT_EQ(getsockopt(fd.get(), SOL_SOCKET, SO_ACCEPTCONN, &got, &got_len), 0)
        << strerror(errno);
    ASSERT_EQ(got_len, sizeof(got));
    ASSERT_EQ(got, expected);
  };

  {
    SCOPED_TRACE("initial");
    ASSERT_NO_FATAL_FAILURE(assert_so_accept_conn_eq(0));
  }

  {
    const sockaddr_in addr = LoopbackSockaddrV4(0);
    ASSERT_EQ(bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);
  }

  {
    SCOPED_TRACE("bound");
    ASSERT_NO_FATAL_FAILURE(assert_so_accept_conn_eq(0));
  }

  ASSERT_EQ(listen(fd.get(), 0), 0) << strerror(errno);

  {
    SCOPED_TRACE("listening");
    ASSERT_NO_FATAL_FAILURE(assert_so_accept_conn_eq(1));
  }

  ASSERT_EQ(shutdown(fd.get(), SHUT_WR), 0) << strerror(errno);

  {
    SCOPED_TRACE("shutdown-write");
    ASSERT_NO_FATAL_FAILURE(assert_so_accept_conn_eq(1));
  }

  ASSERT_EQ(shutdown(fd.get(), SHUT_RD), 0) << strerror(errno);

  // TODO(https://fxbug.dev/61714): Shutting down a listening endpoint is asynchronous in gVisor;
  // transitioning out of the listening state is the responsibility of
  // tcp.endpoint.protocolListenLoop
  // (https://cs.opensource.google/gvisor/gvisor/+/master:pkg/tcpip/transport/tcp/accept.go;l=742-762;drc=58b9bdfc21e792c5d529ec9f4ab0b2f2cd1ee082),
  // which is merely notified when tcp.endpoint.shutdown is called
  // (https://cs.opensource.google/gvisor/gvisor/+/master:pkg/tcpip/transport/tcp/endpoint.go;l=2493;drc=58b9bdfc21e792c5d529ec9f4ab0b2f2cd1ee082).
  if (!kIsFuchsia) {
    SCOPED_TRACE("shutdown-read");
    ASSERT_NO_FATAL_FAILURE(assert_so_accept_conn_eq(0));
  }
}

// Test socket reads on disconnected stream sockets.
TEST(NetStreamTest, DisconnectedRead) {
  fbl::unique_fd socketfd;
  ASSERT_TRUE(socketfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  timeval tv = {
      // Use minimal non-zero timeout as we expect the blocking recv to return before it actually
      // starts reading. Without the timeout, the test could deadlock on a blocking recv, when the
      // underlying code is broken.
      .tv_usec = 1u,
  };
  EXPECT_EQ(setsockopt(socketfd.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0)
      << strerror(errno);
  // Test blocking socket read.
  EXPECT_EQ(recvfrom(socketfd.get(), nullptr, 0, 0, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);
  // Test with MSG_PEEK.
  EXPECT_EQ(recvfrom(socketfd.get(), nullptr, 0, MSG_PEEK, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);

  // Test non blocking socket read.
  ASSERT_NO_FATAL_FAILURE(SetBlocking(socketfd.get(), false));
  EXPECT_EQ(recvfrom(socketfd.get(), nullptr, 0, 0, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);
  // Test with MSG_PEEK.
  EXPECT_EQ(recvfrom(socketfd.get(), nullptr, 0, MSG_PEEK, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);
  EXPECT_EQ(close(socketfd.release()), 0) << strerror(errno);
}

enum class CloseTarget {
  CLIENT,
  SERVER,
};

constexpr const char* CloseTargetToString(const CloseTarget s) {
  switch (s) {
    case CloseTarget::CLIENT:
      return "Client";
    case CloseTarget::SERVER:
      return "Server";
  }
}

using BlockedIOParams = std::tuple<IOMethod, CloseTarget, bool>;

class BlockedIOTest : public NetStreamSocketsTest,
                      public testing::WithParamInterface<BlockedIOParams> {};

TEST_P(BlockedIOTest, CloseWhileBlocked) {
  auto const& [io_method, close_target, linger_enabled] = GetParam();

  bool is_write = io_method.isWrite();

  if (kIsFuchsia && is_write) {
    GTEST_SKIP() << "TODO(https://fxbug.dev/60337): Enable socket write methods after we are able "
                    "to deterministically block on socket writes.";
  }

  // If linger is enabled, closing the socket will cause a TCP RST (by definition).
  bool close_rst = linger_enabled;
  if (is_write) {
    // Fill the send buffer of the client socket to cause write to block.
    ASSERT_NO_FATAL_FAILURE(fill_stream_send_buf(client().get(), server().get(), nullptr));
    // Buffes are full. Closing the socket will now cause a TCP RST.
    close_rst = true;
  }

  // While blocked in I/O, close the peer.
  std::latch fut_started(1);
  // NB: lambdas are not allowed to capture reference to local binding declared
  // in enclosing function.
  const auto fut = std::async(std::launch::async, [&, op = io_method]() {
    fut_started.count_down();

    char c;
    if (close_rst) {
      ASSERT_EQ(op.ExecuteIO(client().get(), &c, sizeof(c)), -1);
      EXPECT_EQ(errno, ECONNRESET) << strerror(errno);
    } else {
      ASSERT_EQ(op.ExecuteIO(client().get(), &c, sizeof(c)), 0) << strerror(errno);
    }
  });
  fut_started.wait();
  ASSERT_NO_FATAL_FAILURE(AssertBlocked(fut));

  // When enabled, causes `close` to send a TCP RST.
  linger opt = {
      .l_onoff = linger_enabled,
      .l_linger = 0,
  };

  switch (close_target) {
    case CloseTarget::CLIENT: {
      ASSERT_EQ(setsockopt(client().get(), SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)), 0)
          << strerror(errno);

      int fd = client().release();

      EXPECT_EQ(close(fd), 0) << strerror(errno);

      // Closing the file descriptor does not interrupt the pending I/O.
      ASSERT_NO_FATAL_FAILURE(AssertBlocked(fut));

      // The pending I/O is still blocked, but the file descriptor is gone.
      ASSERT_EQ(fsync(fd), -1) << strerror(errno);
      ASSERT_EQ(errno, EBADF) << errno;

      [[fallthrough]];  // to unblock the future.
    }
    case CloseTarget::SERVER: {
      ASSERT_EQ(setsockopt(server().get(), SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)), 0)
          << strerror(errno);
      EXPECT_EQ(close(server().release()), 0) << strerror(errno);
      break;
    }
  }
  ASSERT_EQ(fut.wait_for(kTimeout), std::future_status::ready);

  auto undo = DisableSigPipe(is_write);

  char c;
  switch (close_target) {
    case CloseTarget::CLIENT: {
      ASSERT_EQ(io_method.ExecuteIO(client().get(), &c, sizeof(c)), -1);
      EXPECT_EQ(errno, EBADF) << strerror(errno);
      break;
    }
    case CloseTarget::SERVER: {
      if (is_write) {
        ASSERT_EQ(io_method.ExecuteIO(client().get(), &c, sizeof(c)), -1);
        EXPECT_EQ(errno, EPIPE) << strerror(errno);
      } else {
        ASSERT_EQ(io_method.ExecuteIO(client().get(), &c, sizeof(c)), 0) << strerror(errno);
      }
      break;
    }
  }
}

std::string BlockedIOParamsToString(const testing::TestParamInfo<BlockedIOParams> info) {
  // NB: this is a freestanding function because ured binding declarations are not allowed in
  // lambdas.
  auto const& [io_method, close_target, linger_enabled] = info.param;
  std::stringstream s;
  s << "close" << CloseTargetToString(close_target) << "Linger";
  if (linger_enabled) {
    s << "Foreground";
  } else {
    s << "Background";
  }
  s << "During" << io_method.IOMethodToString();

  return s.str();
}

INSTANTIATE_TEST_SUITE_P(NetStreamTest, BlockedIOTest,
                         testing::Combine(testing::ValuesIn(kAllIOMethods),
                                          testing::Values(CloseTarget::CLIENT, CloseTarget::SERVER),
                                          testing::Values(false, true)),
                         BlockedIOParamsToString);

class ListenBacklogTest : public testing::TestWithParam<int> {};

TEST_P(ListenBacklogTest, BacklogValues) {
  sockaddr_in addr = LoopbackSockaddrV4(0);
  socklen_t addrlen = sizeof(addr);

  fbl::unique_fd listenfd;
  ASSERT_TRUE(listenfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(bind(listenfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(getsockname(listenfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));
  ASSERT_EQ(listen(listenfd.get(), GetParam()), 0) << strerror(errno);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(connfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  fbl::unique_fd acceptfd;
  ASSERT_TRUE(acceptfd = fbl::unique_fd(accept(listenfd.get(), nullptr, nullptr)))
      << strerror(errno);

  EXPECT_EQ(close(acceptfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(connfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(listenfd.release()), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(
    NetStreamTest, ListenBacklogTest,
    testing::Values(std::numeric_limits<int>::min(), std::numeric_limits<int16_t>::min(), -1, 0, 1,
                    std::numeric_limits<int16_t>::max() - 1, std::numeric_limits<int16_t>::max(),
                    std::numeric_limits<int>::max() - 1, std::numeric_limits<int>::max()));

using DomainAndPreexistingError = std::tuple<SocketDomain, bool>;

std::string DomainAndPreexistingErrorToString(
    const testing::TestParamInfo<DomainAndPreexistingError>& info) {
  auto const& [domain, preexisting_err] = info.param;
  std::ostringstream oss;
  oss << socketDomainToString(domain);
  oss << '_';
  if (preexisting_err) {
    oss << "WithPreexistingErr";
  } else {
    oss << "NoPreexistingErr";
  }
  return oss.str();
}

class ConnectAcrossIpVersionTest : public testing::TestWithParam<DomainAndPreexistingError> {
 protected:
  void SetUp() override {
    auto const& [domain, preexisting_err] = GetParam();
    ASSERT_TRUE(fd_ = fbl::unique_fd(socket(domain.Get(), SOCK_STREAM, 0))) << strerror(errno);
    auto [addr, addrlen] = LoopbackSockaddrAndSocklenForDomain(domain);
    ASSERT_EQ(bind(fd_.get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), 0)
        << strerror(errno);
  }

  void TearDown() override { EXPECT_EQ(close(fd_.release()), 0) << strerror(errno); }

  const fbl::unique_fd& fd() { return fd_; }

 private:
  fbl::unique_fd fd_;
};

TEST_P(ConnectAcrossIpVersionTest, ConnectReturnsError) {
  auto const& [domain, preexisting_err] = GetParam();

  if (preexisting_err) {
    // Here, we connect to a nonexistent address to trigger an error on the socket.
    // The socket is set to be nonblocking so that the error is delivered asynchronously
    // and therefore is parked on the socket later in the test.
    ASSERT_NO_FATAL_FAILURE(SetBlocking(fd().get(), false));

    auto [addr, addrlen] = LoopbackSockaddrAndSocklenForDomain(domain);
    ASSERT_EQ(connect(fd().get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), -1);
    ASSERT_EQ(errno, EINPROGRESS) << strerror(errno);
    ASSERT_NO_FATAL_FAILURE(AssertExpectedReventsAfterPeerShutdown(fd().get()));
  }

  SocketDomain other_domain = [domain = domain]() {
    switch (domain.which()) {
      case SocketDomain::Which::IPv4:
        return SocketDomain::IPv6();
      case SocketDomain::Which::IPv6:
        return SocketDomain::IPv4();
    }
  }();

  auto [addr, addrlen] = LoopbackSockaddrAndSocklenForDomain(other_domain);
  ASSERT_EQ(connect(fd().get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), -1);

  if (!kIsFuchsia) {
    if (preexisting_err) {
      // TODO(https://fxbug.dev/108729): Match Linux by returning async errors before
      // address errors.
      ASSERT_EQ(errno, ECONNREFUSED) << strerror(errno);
    } else {
      // TODO(https://fxbug.dev/108665): Match Linux by returning divergent errors between
      // IP versions.
      switch (domain.which()) {
        case SocketDomain::Which::IPv4:
          ASSERT_EQ(errno, EAFNOSUPPORT) << strerror(errno);
          break;
        case SocketDomain::Which::IPv6:
          ASSERT_EQ(errno, EINVAL) << strerror(errno);
          break;
      }
    }
  } else {
    ASSERT_EQ(errno, EAFNOSUPPORT) << strerror(errno);
  }
}

INSTANTIATE_TEST_SUITE_P(
    NetStreamTest, ConnectAcrossIpVersionTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()), testing::Bool()),
    DomainAndPreexistingErrorToString);

// Note: we choose 100 because the max number of fds per process is limited to
// 256.
const int32_t kListeningSockets = 100;

TEST(NetStreamTest, MultipleListeningSockets) {
  fbl::unique_fd listenfds[kListeningSockets];
  fbl::unique_fd connfd[kListeningSockets];

  sockaddr_in addr = LoopbackSockaddrV4(0);
  socklen_t addrlen = sizeof(addr);

  for (auto& listenfd : listenfds) {
    ASSERT_TRUE(listenfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

    ASSERT_EQ(bind(listenfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);

    ASSERT_EQ(listen(listenfd.get(), 0), 0) << strerror(errno);
  }

  for (int i = 0; i < kListeningSockets; i++) {
    ASSERT_EQ(getsockname(listenfds[i].get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(addr));

    ASSERT_TRUE(connfd[i] = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

    ASSERT_EQ(connect(connfd[i].get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);
  }

  for (int i = 0; i < kListeningSockets; i++) {
    ASSERT_EQ(0, close(connfd[i].release()));
    ASSERT_EQ(0, close(listenfds[i].release()));
  }
}

// Test the behavior of poll on an unconnected or non-listening stream socket.
TEST(NetStreamTest, UnconnectPoll) {
  fbl::unique_fd init, bound;
  ASSERT_TRUE(init = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_TRUE(bound = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(bound.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  constexpr short masks[] = {
      0,
      POLLIN | POLLOUT | POLLPRI | POLLRDHUP,
  };
  for (short events : masks) {
    pollfd pfds[] = {{
                         .fd = init.get(),
                         .events = events,
                     },
                     {
                         .fd = bound.get(),
                         .events = events,
                     }};
    int n = poll(pfds, std::size(pfds), std::chrono::milliseconds(kTimeout).count());
    EXPECT_GE(n, 0) << strerror(errno);
    EXPECT_EQ(n, static_cast<int>(std::size(pfds))) << " events = " << std::hex << events;

    for (size_t i = 0; i < std::size(pfds); i++) {
      EXPECT_EQ(pfds[i].revents, (events & POLLOUT) | POLLHUP) << i;
    }
  }

  // Poll on listening socket does timeout on no incoming connections.
  ASSERT_EQ(listen(bound.get(), 0), 0) << strerror(errno);
  pollfd pfd = {
      .fd = bound.get(),
  };
  EXPECT_EQ(poll(&pfd, 1, 0), 0) << strerror(errno);
}

TEST(NetStreamTest, ConnectTwice) {
  fbl::unique_fd client, listener;
  ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  ASSERT_EQ(connect(client.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), -1);
  ASSERT_EQ(errno, ECONNREFUSED) << strerror(errno);

  ASSERT_EQ(listen(listener.get(), 0), 0) << strerror(errno);

  // TODO(https://fxbug.dev/61594): decide if we want to match Linux's behaviour.
  {
    int ret = connect(client.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (kIsFuchsia) {
      ASSERT_EQ(ret, -1);
      ASSERT_EQ(errno, ECONNABORTED) << strerror(errno);
    } else {
      ASSERT_EQ(ret, 0);
    }
  }

  EXPECT_EQ(close(listener.release()), 0) << strerror(errno);
  EXPECT_EQ(close(client.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, ConnectCloseRace) {
  sockaddr_in addr = LoopbackSockaddrV4(0);
  // Use the ephemeral port allocated by the stack as destination address for connect.
  {
    fbl::unique_fd tmp;
    ASSERT_TRUE(tmp = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

    ASSERT_EQ(bind(tmp.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);
    socklen_t addrlen = sizeof(addr);
    ASSERT_EQ(getsockname(tmp.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(addr));
  }

  std::array<std::thread, 50> threads;
  for (auto& t : threads) {
    t = std::thread([&] {
      for (int i = 0; i < 5; i++) {
        fbl::unique_fd client;
        ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
            << strerror(errno);

        ASSERT_EQ(connect(client.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
                  -1);
        if (kIsFuchsia) {
          ASSERT_EQ(errno, EINPROGRESS) << strerror(errno);
        } else {
          // Linux could return ECONNREFUSED if it processes the incoming RST
          // before connect system call returns.
          ASSERT_TRUE(errno == EINPROGRESS || errno == ECONNREFUSED) << strerror(errno);
        }
        EXPECT_EQ(close(client.release()), 0) << strerror(errno);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

enum class HangupMethod {
  kClose,
  kShutdown,
};

constexpr const char* HangupMethodToString(const HangupMethod s) {
  switch (s) {
    case HangupMethod::kClose:
      return "Close";
    case HangupMethod::kShutdown:
      return "Shutdown";
  }
}

void ExpectLastError(const fbl::unique_fd& fd, int expected) {
  int err;
  socklen_t optlen = sizeof(err);
  ASSERT_EQ(getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
  ASSERT_EQ(optlen, sizeof(err));
  EXPECT_EQ(err, expected) << " err=" << strerror(err) << " expected=" << strerror(expected);
}

using HangupParams = std::tuple<CloseTarget, HangupMethod>;

class HangupTest : public testing::TestWithParam<HangupParams> {};

TEST_P(HangupTest, DuringConnect) {
  auto const& [close_target, hangup_method] = GetParam();

  fbl::unique_fd listener;
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  sockaddr_in addr_in = LoopbackSockaddrV4(0);
  auto* addr = reinterpret_cast<sockaddr*>(&addr_in);
  socklen_t addr_len = sizeof(addr_in);

  ASSERT_EQ(bind(listener.get(), addr, addr_len), 0) << strerror(errno);
  {
    socklen_t addr_len_in = addr_len;
    ASSERT_EQ(getsockname(listener.get(), addr, &addr_len), 0) << strerror(errno);
    EXPECT_EQ(addr_len, addr_len_in);
  }
  ASSERT_EQ(listen(listener.get(), 0), 0) << strerror(errno);

  fbl::unique_fd established_client;
  ASSERT_TRUE(established_client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0)))
      << strerror(errno);
  ASSERT_EQ(connect(established_client.get(), addr, addr_len), 0) << strerror(errno);

  // Ensure that the accept queue has the completed connection.
  {
    pollfd pfd = {
        .fd = listener.get(),
        .events = POLLIN,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(pfd.revents, POLLIN);
  }

  // Connect asynchronously since this one will end up in SYN-SENT.
  fbl::unique_fd connecting_client;
  ASSERT_TRUE(connecting_client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);
  EXPECT_EQ(connect(connecting_client.get(), addr, addr_len), -1);
  EXPECT_EQ(errno, EINPROGRESS) << strerror(errno);

  switch (close_target) {
    case CloseTarget::CLIENT:
      switch (hangup_method) {
        case HangupMethod::kClose: {
          EXPECT_EQ(close(established_client.release()), 0) << strerror(errno);
          // Closing the established client isn't enough; the connection must be accepted before
          // the connecting client can make progress.
          EXPECT_EQ(connect(connecting_client.get(), addr, addr_len), -1) << strerror(errno);
          EXPECT_EQ(errno, EALREADY) << strerror(errno);

          EXPECT_EQ(close(connecting_client.release()), 0) << strerror(errno);

          // Established connection is still in the accept queue, even though it's closed.
          fbl::unique_fd accepted;
          EXPECT_TRUE(accepted = fbl::unique_fd(accept(listener.get(), nullptr, nullptr)))
              << strerror(errno);

          // Incomplete connection never made it into the queue.
          EXPECT_FALSE(accepted = fbl::unique_fd(accept(listener.get(), nullptr, nullptr)));
          EXPECT_EQ(errno, EAGAIN) << strerror(errno);

          break;
        }
        case HangupMethod::kShutdown: {
          ASSERT_EQ(shutdown(connecting_client.get(), SHUT_RD), 0) << strerror(errno);

          {
            pollfd pfd = {
                .fd = connecting_client.get(),
                .events = std::numeric_limits<decltype(pfd.events)>::max(),
            };
            if (!kIsFuchsia) {
              int n = poll(&pfd, 1, 0);
              EXPECT_GE(n, 0) << strerror(errno);
              EXPECT_EQ(n, 1);
              EXPECT_EQ(pfd.revents, POLLOUT | POLLWRNORM | POLLHUP | POLLERR);
            } else {
              // TODO(https://fxbug.dev/81448): Poll for POLLIN and POLLRDHUP to show their absence.
              // Can't be polled now because these events are asserted synchronously, and they might
              // be ready before the other expected events are asserted.
              pfd.events ^= (POLLIN | POLLRDHUP);
              // TODO(https://fxbug.dev/85279): Remove the poll timeout.
              int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
              EXPECT_GE(n, 0) << strerror(errno);
              EXPECT_EQ(n, 1);
              // TODO(https://fxbug.dev/73258): Add POLLWRNORM to the expectations.
              EXPECT_EQ(pfd.revents, POLLOUT | POLLHUP | POLLERR);
            }
          }

          EXPECT_EQ(connect(connecting_client.get(), addr, addr_len), -1);
          if (!kIsFuchsia) {
            EXPECT_EQ(errno, EINPROGRESS) << strerror(errno);
          } else {
            // TODO(https://fxbug.dev/61594): Fuchsia doesn't allow never-connected socket reuse.
            EXPECT_EQ(errno, ECONNRESET) << strerror(errno);
          }
          // connect result was consumed by the connect call.
          ASSERT_NO_FATAL_FAILURE(ExpectLastError(connecting_client, 0));

          ASSERT_EQ(shutdown(established_client.get(), SHUT_RD), 0) << strerror(errno);

          {
            pollfd pfd = {
                .fd = established_client.get(),
                .events = POLLIN,
            };
            int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
            EXPECT_GE(n, 0) << strerror(errno);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfd.revents, POLLIN);
          }

          EXPECT_EQ(connect(established_client.get(), addr, addr_len), -1);
          EXPECT_EQ(errno, EISCONN) << strerror(errno);
          ASSERT_NO_FATAL_FAILURE(ExpectLastError(established_client, 0));

          break;
        }
      }
      break;
    case CloseTarget::SERVER: {
      switch (hangup_method) {
        case HangupMethod::kClose:
          EXPECT_EQ(close(listener.release()), 0) << strerror(errno);
          break;
        case HangupMethod::kShutdown: {
          ASSERT_EQ(shutdown(listener.get(), SHUT_RD), 0) << strerror(errno);
          pollfd pfd = {
              .fd = listener.get(),
              .events = std::numeric_limits<decltype(pfd.events)>::max(),
          };
          if (!kIsFuchsia) {
            int n = poll(&pfd, 1, 0);
            EXPECT_GE(n, 0) << strerror(errno);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfd.revents, POLLOUT | POLLWRNORM | POLLHUP);
          } else {
            // TODO(https://fxbug.dev/81448): Poll for POLLIN and POLLRDHUP to show their absence.
            // Can't be polled now because these events are asserted synchronously, and they might
            // be ready before the other expected events are asserted.
            pfd.events ^= (POLLIN | POLLRDHUP);
            // TODO(https://fxbug.dev/85279): Remove the poll timeout.
            int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
            EXPECT_GE(n, 0) << strerror(errno);
            EXPECT_EQ(n, 1);
            // TODO(https://fxbug.dev/85283): Remove POLLERR from the expectations.
            // TODO(https://fxbug.dev/73258): Add POLLWRNORM to the expectations.
            EXPECT_EQ(pfd.revents, POLLOUT | POLLHUP | POLLERR);
          }
          break;
        }
      }

      const struct {
        const fbl::unique_fd& fd;
        const int connect_result;
        const int last_error;
      } expectations[] = {
          {
              .fd = established_client,
              // We're doing the wrong thing here. Broadly what seems to be happening:
              // - closing the listener causes a RST to be sent
              // - when RST is received, the endpoint moves to an error state
              // - loop{Read,Write} observes the error and stores it in the terminal error
              // - tcpip.Endpoint.Connect returns ErrConnectionAborted
              //   - the terminal error is returned
              //
              // Linux seems to track connectedness separately from the TCP state machine state;
              // when an endpoint becomes connected, it never becomes unconnected with respect to
              // the behavior of `connect`.
              //
              // Since the call to tcpip.Endpoint.Connect does the wrong thing, this is likely a
              // gVisor bug.
              .connect_result = kIsFuchsia ? ECONNRESET : EISCONN,
              .last_error = kIsFuchsia ? 0 : ECONNRESET,
          },
          {
              .fd = connecting_client,
              .connect_result = ECONNREFUSED,
              .last_error = 0,
          },
      };

      for (size_t i = 0; i < std::size(expectations); i++) {
        SCOPED_TRACE("i=" + std::to_string(i));

        const auto& expected = expectations[i];
        ASSERT_NO_FATAL_FAILURE(AssertExpectedReventsAfterPeerShutdown(expected.fd.get()));
        EXPECT_EQ(connect(expected.fd.get(), addr, addr_len), -1);
        EXPECT_EQ(errno, expected.connect_result)
            << " errno=" << strerror(errno) << " expected=" << strerror(expected.connect_result);

        ASSERT_NO_FATAL_FAILURE(ExpectLastError(expected.fd, expected.last_error));
      }

      break;
    }
  }
}

std::string HangupParamsToString(const testing::TestParamInfo<HangupParams> info) {
  auto const& [close_target, hangup_method] = info.param;
  std::stringstream s;
  s << HangupMethodToString(hangup_method);
  s << CloseTargetToString(close_target);
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(NetStreamTest, HangupTest,
                         testing::Combine(testing::Values(CloseTarget::CLIENT, CloseTarget::SERVER),
                                          testing::Values(HangupMethod::kClose,
                                                          HangupMethod::kShutdown)),
                         HangupParamsToString);

TEST(LocalhostTest, Accept) {
  fbl::unique_fd serverfd;
  ASSERT_TRUE(serverfd = fbl::unique_fd(socket(AF_INET6, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in6 serveraddr = LoopbackSockaddrV6(0);
  socklen_t serveraddrlen = sizeof(serveraddr);
  ASSERT_EQ(bind(serverfd.get(), reinterpret_cast<sockaddr*>(&serveraddr), serveraddrlen), 0)
      << strerror(errno);
  ASSERT_EQ(getsockname(serverfd.get(), reinterpret_cast<sockaddr*>(&serveraddr), &serveraddrlen),
            0)
      << strerror(errno);
  ASSERT_EQ(serveraddrlen, sizeof(serveraddr));
  ASSERT_EQ(listen(serverfd.get(), 0), 0) << strerror(errno);

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET6, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), reinterpret_cast<sockaddr*>(&serveraddr), serveraddrlen), 0)
      << strerror(errno);

  sockaddr_in connaddr;
  socklen_t connaddrlen = sizeof(connaddr);
  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(
                  accept(serverfd.get(), reinterpret_cast<sockaddr*>(&connaddr), &connaddrlen)))
      << strerror(errno);
  ASSERT_GT(connaddrlen, sizeof(connaddr));
}

TEST(LocalhostTest, AcceptAfterReset) {
  fbl::unique_fd server;
  ASSERT_TRUE(server = fbl::unique_fd(socket(AF_INET6, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in6 addr = LoopbackSockaddrV6(0);
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(bind(server.get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(getsockname(server.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));
  ASSERT_EQ(listen(server.get(), 0), 0) << strerror(errno);

  {
    fbl::unique_fd client;
    ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET6, SOCK_STREAM, 0))) << strerror(errno);
    ASSERT_EQ(connect(client.get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), 0)
        << strerror(errno);
    linger opt = {
        .l_onoff = 1,
        .l_linger = 0,
    };
    ASSERT_EQ(setsockopt(client.get(), SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)), 0)
        << strerror(errno);

    // Ensure the accept queue has the passive connection enqueued before attempting to reset it.
    pollfd pfd = {
        .fd = server.get(),
        .events = POLLIN,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(pfd.revents, POLLIN);

    // Close the client and trigger a RST.
    EXPECT_EQ(close(client.release()), 0) << strerror(errno);
  }

  fbl::unique_fd conn;
  ASSERT_TRUE(
      conn = fbl::unique_fd(accept(server.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen)))
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));
  ASSERT_EQ(addr.sin6_family, AF_INET6);
  char buf[INET6_ADDRSTRLEN];
  ASSERT_TRUE(IN6_IS_ADDR_LOOPBACK(&addr.sin6_addr))
      << inet_ntop(addr.sin6_family, &addr.sin6_addr, buf, sizeof(buf));
  ASSERT_NE(addr.sin6_port, 0);

  // Wait for the connection to close to avoid flakes when this code is reached before the RST
  // arrives at |conn|.
  {
    pollfd pfd = {
        .fd = conn.get(),
    };

    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(pfd.revents, POLLERR | POLLHUP);
  }

  int err;
  socklen_t optlen = sizeof(err);
  ASSERT_EQ(getsockopt(conn.get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
  ASSERT_EQ(optlen, sizeof(err));
  ASSERT_EQ(err, ECONNRESET) << strerror(err);
}

TEST(LocalhostTest, RaceLocalPeerClose) {
  fbl::unique_fd listener;
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  if (!kIsFuchsia) {
    // Make the listener non-blocking so that we can let accept system call return
    // below when there are no acceptable connections.
    ASSERT_NO_FATAL_FAILURE(SetBlocking(listener.get(), false));
  }
  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  std::array<std::thread, 50> threads;
  ASSERT_EQ(listen(listener.get(), threads.size()), 0) << strerror(errno);

  // Run many iterations in parallel in order to increase load on Netstack and increase the
  // probability we'll hit the problem.
  for (auto& t : threads) {
    t = std::thread([&] {
      fbl::unique_fd peer;
      ASSERT_TRUE(peer = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

      // Connect and immediately close a peer with linger. This causes the network-initiated
      // close that will race with the accepted connection close below. Linger is necessary
      // because we need a TCP RST to force a full teardown, tickling Netstack the right way to
      // cause a bad race.
      linger opt = {
          .l_onoff = 1,
          .l_linger = 0,
      };
      EXPECT_EQ(setsockopt(peer.get(), SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)), 0)
          << strerror(errno);
      ASSERT_EQ(connect(peer.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
          << strerror(errno);
      EXPECT_EQ(close(peer.release()), 0) << strerror(errno);

      // Accept the connection and close it, adding new racing signal (operating on `close`) to
      // Netstack.
      auto local = fbl::unique_fd(accept(listener.get(), nullptr, nullptr));
      if (!local.is_valid()) {
        if (kIsFuchsia) {
          FAIL() << strerror(errno);
        }
        // We get EAGAIN when there are no pending acceptable connections. Though the peer
        // connect was a blocking call, it can return before the final ACK is sent out causing
        // the RST from linger0+close to be sent out before the final ACK. This would result in
        // that connection to be not completed and hence not added to the acceptable queue.
        //
        // The above race does not currently exist on Fuchsia where the final ACK would always
        // be sent out over lo before connect() call returns.
        ASSERT_EQ(errno, EAGAIN) << strerror(errno);
      } else {
        EXPECT_EQ(close(local.release()), 0) << strerror(errno);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(close(listener.release()), 0) << strerror(errno);
}

TEST(LocalhostTest, GetAddrInfo) {
  addrinfo hints = {
      .ai_family = AF_UNSPEC,
      .ai_socktype = SOCK_STREAM,
  };

  addrinfo* result;
  ASSERT_EQ(getaddrinfo("localhost", nullptr, &hints, &result), 0) << strerror(errno);

  int i = 0;
  for (addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
    i++;

    EXPECT_EQ(ai->ai_socktype, hints.ai_socktype);
    const sockaddr* sa = ai->ai_addr;

    switch (ai->ai_family) {
      case AF_INET: {
        EXPECT_EQ(ai->ai_addrlen, (socklen_t)16);

        unsigned char expected_addr[4] = {0x7f, 0x00, 0x00, 0x01};

        auto sin = reinterpret_cast<const sockaddr_in*>(sa);
        EXPECT_EQ(sin->sin_addr.s_addr, *reinterpret_cast<uint32_t*>(expected_addr));

        break;
      }
      case AF_INET6: {
        EXPECT_EQ(ai->ai_addrlen, (socklen_t)28);

        const char expected_addr[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

        auto sin6 = reinterpret_cast<const sockaddr_in6*>(sa);
        EXPECT_STREQ(reinterpret_cast<const char*>(sin6->sin6_addr.s6_addr), expected_addr);

        break;
      }
    }
  }
  EXPECT_EQ(i, 2);
  freeaddrinfo(result);
}

class IOMethodTest : public testing::TestWithParam<IOMethod> {};

TEST_P(IOMethodTest, UnconnectedSocketIO) {
  fbl::unique_fd sockfd;
  ASSERT_TRUE(sockfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  IOMethod io_method = GetParam();
  char buffer[1];
  bool is_write = io_method.isWrite();
  auto undo = DisableSigPipe(is_write);
  ASSERT_EQ(io_method.ExecuteIO(sockfd.get(), buffer, sizeof(buffer)), -1);
  if (is_write) {
    ASSERT_EQ(errno, EPIPE) << strerror(errno);
  } else {
    ASSERT_EQ(errno, ENOTCONN) << strerror(errno);
  }
}

TEST_P(IOMethodTest, ListenerSocketIO) {
  fbl::unique_fd listener;
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in serveraddr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<sockaddr*>(&serveraddr), sizeof(serveraddr)), 0)
      << strerror(errno);
  ASSERT_EQ(listen(listener.get(), 0), 0) << strerror(errno);

  IOMethod io_method = GetParam();
  char buffer[1];
  bool is_write = io_method.isWrite();
  auto undo = DisableSigPipe(is_write);
  ASSERT_EQ(io_method.ExecuteIO(listener.get(), buffer, sizeof(buffer)), -1);
  if (is_write) {
    ASSERT_EQ(errno, EPIPE) << strerror(errno);
  } else {
    ASSERT_EQ(errno, ENOTCONN) << strerror(errno);
  }
}

TEST_P(IOMethodTest, NullptrFaultSTREAM) {
  fbl::unique_fd listener, client, server;
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  ASSERT_EQ(listen(listener.get(), 0), 0) << strerror(errno);

  ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);
  int ret;
  EXPECT_EQ(ret = connect(client.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
            -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

    pollfd pfd = {
        .fd = client.get(),
        .events = POLLOUT,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
  }

  ASSERT_TRUE(server = fbl::unique_fd(accept4(listener.get(), nullptr, nullptr, SOCK_NONBLOCK)))
      << strerror(errno);
  EXPECT_EQ(close(listener.release()), 0) << strerror(errno);

  DoNullPtrIO(client, server, GetParam(), false);
}

INSTANTIATE_TEST_SUITE_P(IOMethodTests, IOMethodTest, testing::ValuesIn(kAllIOMethods),
                         [](const auto info) { return info.param.IOMethodToString(); });

}  // namespace
