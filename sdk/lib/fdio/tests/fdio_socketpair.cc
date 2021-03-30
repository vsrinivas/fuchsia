// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/clock.h>
#include <lib/zx/thread.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <array>
#include <future>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

class SocketPair : public testing::TestWithParam<uint16_t> {
 protected:
  void SetUp() override {
    int int_fds[fds_.size()];
    ASSERT_EQ(socketpair(AF_UNIX, GetParam(), 0, int_fds), 0) << strerror(errno);
    for (size_t i = 0; i < fds_.size(); ++i) {
      fds_[i].reset(int_fds[i]);
    }
  }

  const std::array<fbl::unique_fd, 2>& fds() { return fds_; }
  std::array<fbl::unique_fd, 2>& mutable_fds() { return fds_; }

 private:
  std::array<fbl::unique_fd, 2> fds_;
};

std::string TypeToString(const testing::TestParamInfo<uint16_t>& info) {
  switch (info.param) {
    case SOCK_STREAM:
      return "Stream";
    case SOCK_DGRAM:
      return "Datagram";
    default:
      return testing::PrintToStringParamName()(info);
  }
}

TEST_P(SocketPair, Control) {
  // write() and read() should work.
  constexpr char buf[] = "abc";
  ASSERT_EQ(write(fds()[0].get(), buf, sizeof(buf)), ssize_t(sizeof(buf))) << strerror(errno);

  char recvbuf[sizeof(buf) + 1];
  ASSERT_EQ(read(fds()[1].get(), recvbuf, sizeof(recvbuf)), ssize_t(sizeof(buf)))
      << strerror(errno);
  ASSERT_STREQ(recvbuf, buf);

  // send() and recv() should also work.
  ASSERT_EQ(send(fds()[1].get(), buf, sizeof(buf), 0), ssize_t(sizeof(buf))) << strerror(errno);

  ASSERT_EQ(recv(fds()[0].get(), recvbuf, sizeof(recvbuf), 0), ssize_t(sizeof(buf)))
      << strerror(errno);

  recvbuf[sizeof(recvbuf) - 1] = 0;
  ASSERT_STREQ(recvbuf, buf);
  EXPECT_EQ(close(mutable_fds()[0].release()), 0) << strerror(errno);
  EXPECT_EQ(close(mutable_fds()[1].release()), 0) << strerror(errno);
}

static_assert(EAGAIN == EWOULDBLOCK, "Assuming EAGAIN and EWOULDBLOCK have same value");

#if defined(__Fuchsia__)
#define SEND_FLAGS 0
#else
#define SEND_FLAGS MSG_NOSIGNAL
#endif

class SocketPairShutdown : public SocketPair {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(SocketPair::SetUp());

    // Set both ends to non-blocking to make testing for readability/writability easier.
    for (size_t i = 0; i < fds().size(); ++i) {
      ASSERT_EQ(fcntl(fds()[i].get(), F_SETFL, O_NONBLOCK), 0) << i << ":" << strerror(errno);
    }

    char buf[1];
    for (size_t i = 0; i < fds().size(); ++i) {
      EXPECT_EQ(read(fds()[i].get(), buf, sizeof(buf)), -1) << i;
      EXPECT_EQ(errno, EAGAIN) << i << ":" << strerror(errno);
    }

    for (size_t i = 0; i < fds().size(); ++i) {
      EXPECT_EQ(write(fds()[i].get(), buf, sizeof(buf)), 1) << i << ":" << strerror(errno);
    }

    for (size_t i = 0; i < fds().size(); ++i) {
      EXPECT_EQ(read(fds()[i].get(), buf, sizeof(buf)), ssize_t(sizeof(buf)))
          << i << ":" << strerror(errno);
    }
  }
};

TEST_P(SocketPairShutdown, Read) {
  // Write a byte into fds()[1] to test for readability later.
  char buf[1];
  EXPECT_EQ(write(fds()[1].get(), buf, sizeof(buf)), ssize_t(sizeof(buf))) << strerror(errno);

  // Close one side down for reading.
  ASSERT_EQ(shutdown(fds()[0].get(), SHUT_RD), 0) << strerror(errno);

  // Can read the byte already written into the pipe.
  EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), ssize_t(sizeof(buf))) << strerror(errno);

  // But not send any further bytes
  EXPECT_EQ(send(fds()[1].get(), buf, sizeof(buf), SEND_FLAGS), -1);
  EXPECT_EQ(errno, EPIPE) << strerror(errno);

  // Or read any more
  EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), 0) << strerror(errno);
  EXPECT_EQ(close(mutable_fds()[0].release()), 0) << strerror(errno);
  EXPECT_EQ(close(mutable_fds()[1].release()), 0) << strerror(errno);
}

TEST_P(SocketPairShutdown, Write) {
  // Close one side down for writing.
  ASSERT_EQ(shutdown(fds()[0].get(), SHUT_WR), 0) << strerror(errno);

  char buf[1];

  // Should still be readable.
  EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), -1);
  EXPECT_EQ(errno, EAGAIN) << strerror(errno);

  // But not writable
  EXPECT_EQ(send(fds()[0].get(), buf, sizeof(buf), SEND_FLAGS), -1);
  EXPECT_EQ(errno, EPIPE) << strerror(errno);

  // Should still be able to write + read a message in the other direction.
  EXPECT_EQ(write(fds()[1].get(), buf, sizeof(buf)), ssize_t(sizeof(buf))) << strerror(errno);
  EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), ssize_t(sizeof(buf))) << strerror(errno);
  EXPECT_EQ(close(mutable_fds()[0].release()), 0) << strerror(errno);
  EXPECT_EQ(close(mutable_fds()[1].release()), 0) << strerror(errno);
}

TEST_P(SocketPairShutdown, ReadWrite) {
  // Close one side for reading and writing.
  ASSERT_EQ(shutdown(fds()[0].get(), SHUT_RDWR), 0) << strerror(errno);

  char buf[1];

  // Writing should fail.
  EXPECT_EQ(send(fds()[0].get(), buf, sizeof(buf), SEND_FLAGS), -1);
  EXPECT_EQ(errno, EPIPE) << strerror(errno);

  // Reading should return no data.
  EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), 0) << strerror(errno);
}

std::thread poll_for_read_with_timeout(const fbl::unique_fd& fd) {
  return std::thread([&]() {
    struct pollfd pfd = {
        .fd = fd.get(),
        .events = POLLIN,
    };

    constexpr std::chrono::duration minimum_duration = std::chrono::milliseconds(500);
    const auto begin = std::chrono::steady_clock::now();
    EXPECT_EQ(poll(&pfd, 1, std::chrono::milliseconds(minimum_duration).count()), 1)
        << strerror(errno);
    EXPECT_LE(std::chrono::steady_clock::now() - begin, minimum_duration);

    int num_readable = 0;
    EXPECT_EQ(ioctl(pfd.fd, FIONREAD, &num_readable), 0) << strerror(errno);
    EXPECT_EQ(num_readable, 0);
  });
}

TEST_P(SocketPairShutdown, SelfWritePoll) {
  std::thread poll_thread = poll_for_read_with_timeout(fds()[0]);

  EXPECT_EQ(shutdown(fds()[0].get(), SHUT_RDWR), 0) << strerror(errno);

  poll_thread.join();
}

TEST_P(SocketPairShutdown, PeerWritePoll) {
  std::thread poll_thread = poll_for_read_with_timeout(fds()[0]);

  EXPECT_EQ(shutdown(fds()[1].get(), SHUT_RDWR), 0) << strerror(errno);

  poll_thread.join();
}

std::thread recv_thread(const fbl::unique_fd& fd) {
  return std::thread([&]() {
    std::array<char, 256> buf;

    EXPECT_EQ(recv(fd.get(), buf.data(), buf.size(), 0), 0) << strerror(errno);
  });
}

TEST_P(SocketPair, SelfReadDuringRecv) {
  std::thread t = recv_thread(fds()[0]);

  EXPECT_EQ(shutdown(fds()[0].get(), SHUT_RD), 0) << strerror(errno);

  t.join();
}

TEST_P(SocketPair, SelfWriteDuringRecv) {
  std::thread t = recv_thread(fds()[0]);

  EXPECT_EQ(shutdown(fds()[1].get(), SHUT_WR), 0) << strerror(errno);

  t.join();
}

std::thread send_thread(const fbl::unique_fd& fd) {
  return std::thread([&]() {
    std::array<char, 256> buf;

    EXPECT_EQ(send(fd.get(), buf.data(), buf.size(), 0), -1);
    EXPECT_EQ(errno, EPIPE) << strerror(errno);
  });
}

constexpr zx::duration kStateCheckIntervals = zx::usec(5);

// Wait until |thread| has entered |state|.
zx_status_t WaitForState(const zx::thread& thread, zx_thread_state_t desired_state) {
  while (true) {
    zx_info_thread_t info;
    zx_status_t status = thread.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      return status;
    }

    if (info.state == desired_state) {
      return ZX_OK;
    }

    zx::nanosleep(zx::deadline_after(kStateCheckIntervals));
  }
}

TEST_P(SocketPair, SelfWriteDuringSend) {
  // First, fill up the socket so the next send() will block.
  std::array<char, 256> buf;
  while (true) {
    ssize_t status = send(fds()[0].get(), buf.data(), buf.size(), MSG_DONTWAIT);
    if (status < 0) {
      ASSERT_EQ(errno, EAGAIN) << strerror(errno);
      break;
    }
  }
  // Then start a thread blocking on a send().
  std::thread t = send_thread(fds()[0]);

  // Wait for the thread to sleep in send.
  ASSERT_OK(WaitForState(*(zx::unowned_thread(thrd_get_zx_handle(t.native_handle()))),
                         ZX_THREAD_STATE_BLOCKED_WAIT_ONE));

  EXPECT_EQ(shutdown(fds()[0].get(), SHUT_WR), 0) << strerror(errno);

  t.join();
}

TEST_P(SocketPair, SelfWriteBeforeSend) {
  // First, fill up the socket so the next send() will block.
  std::array<char, 256> buf;
  while (true) {
    ssize_t status = send(fds()[0].get(), buf.data(), buf.size(), MSG_DONTWAIT);
    if (status < 0) {
      ASSERT_EQ(errno, EAGAIN) << strerror(errno);
      break;
    }
  }

  EXPECT_EQ(shutdown(fds()[0].get(), SHUT_WR), 0) << strerror(errno);

  // Then start a thread blocking on a send().
  std::thread t = send_thread(fds()[0]);

  t.join();
}

TEST_P(SocketPair, PeerReadDuringSend) {
  // First, fill up the socket so the next send() will block.
  std::array<char, 256> buf;
  while (true) {
    ssize_t status = send(fds()[0].get(), buf.data(), buf.size(), MSG_DONTWAIT);
    if (status < 0) {
      ASSERT_EQ(errno, EAGAIN) << strerror(errno);
      break;
    }
  }

  // Then start a thread blocking on a send().
  std::thread t = send_thread(fds()[0]);

  // Wait for the thread to sleep in send.
  ASSERT_OK(WaitForState(*(zx::unowned_thread(thrd_get_zx_handle(t.native_handle()))),
                         ZX_THREAD_STATE_BLOCKED_WAIT_ONE));

  EXPECT_EQ(shutdown(fds()[1].get(), SHUT_RD), 0) << strerror(errno);

  t.join();
}

TEST_P(SocketPair, PeerReadBeforeSend) {
  // First, fill up the socket so the next send() will block.
  std::array<char, 256> buf;
  while (true) {
    ssize_t status = send(fds()[0].get(), buf.data(), buf.size(), MSG_DONTWAIT);
    if (status < 0) {
      ASSERT_EQ(errno, EAGAIN) << strerror(errno);
      break;
    }
  }

  EXPECT_EQ(shutdown(fds()[1].get(), SHUT_RD), 0) << strerror(errno);

  std::thread t = send_thread(fds()[0]);

  t.join();
}

TEST_P(SocketPair, CloneOrUnwrapAndWrap) {
  zx::handle handle;
  ASSERT_OK(fdio_fd_clone(fds()[0].get(), handle.reset_and_get_address()));

  fbl::unique_fd cloned_fd;
  ASSERT_OK(fdio_fd_create(handle.release(), cloned_fd.reset_and_get_address()));

  ASSERT_OK(fdio_fd_transfer(mutable_fds()[0].release(), handle.reset_and_get_address()));

  fbl::unique_fd transferred_fd;
  ASSERT_OK(fdio_fd_create(handle.release(), transferred_fd.reset_and_get_address()));

  // Verify that an operation specific to socketpairs works on these fds.
  ASSERT_EQ(shutdown(transferred_fd.get(), SHUT_WR), 0) << strerror(errno);
  ASSERT_EQ(shutdown(cloned_fd.get(), SHUT_RD), 0) << strerror(errno);
}

// Verify scenario, where multi-segment recvmsg is requested, but the socket has
// just enough data to *completely* fill one segment.
// In this scenario, an attempt to read data for the next segment immediately
// fails with ZX_ERR_SHOULD_WAIT; at this point recvmsg should report total
// number of bytes read, instead of failing with EAGAIN.
TEST_P(SocketPair, StreamRecvmsgNonblockBoundary) {
  ASSERT_EQ(fcntl(fds()[0].get(), F_SETFL, O_NONBLOCK), 0);
  ASSERT_EQ(fcntl(fds()[1].get(), F_SETFL, O_NONBLOCK), 0);

  // Write 4 bytes of data to socket.
  const uint32_t data_out = 0x12345678;
  EXPECT_EQ(write(fds()[0].get(), &data_out, sizeof(data_out)), ssize_t(sizeof(data_out)))
      << strerror(errno);

  uint32_t data_in1, data_in2;
  // Fail at compilation stage if anyone changes types.
  // This is mandatory here: we need the first chunk to be exactly the same
  // length as total size of data we just wrote.
  assert(sizeof(data_in1) == sizeof(data_out));

  struct iovec iov[] = {{
                            .iov_base = &data_in1,
                            .iov_len = sizeof(data_in1),
                        },
                        {
                            .iov_base = &data_in2,
                            .iov_len = sizeof(data_in2),
                        }};

  struct msghdr msg = {
      .msg_iov = iov,
      .msg_iovlen = std::size(iov),
  };

  EXPECT_EQ(recvmsg(fds()[1].get(), &msg, 0), ssize_t(sizeof(data_in1))) << strerror(errno);
}

// Verify scenario, where multi-segment sendmsg is requested, but the socket has
// just enough spare buffer to *completely* read one segment.
// In this scenario, an attempt to send second segment should immediately fail
// with ZX_ERR_SHOULD_WAIT, but the sendmsg should report first segment length
// rather than failing with EAGAIN.
TEST_P(SocketPair, StreamSendmsgNonblockBoundary) {
  if (GetParam() == SOCK_DGRAM) {
    GTEST_SKIP() << "Stream only";
  }

  const ssize_t memlength = 65536;
  std::unique_ptr<uint8_t[]> memchunk(new uint8_t[memlength]);

  struct iovec iov[] = {
      {
          .iov_base = memchunk.get(),
          .iov_len = memlength,
      },
      {
          .iov_base = memchunk.get(),
          .iov_len = memlength,
      },
  };

  ASSERT_EQ(fcntl(fds()[0].get(), F_SETFL, O_NONBLOCK), 0);
  ASSERT_EQ(fcntl(fds()[1].get(), F_SETFL, O_NONBLOCK), 0);

  struct msghdr msg = {
      .msg_iov = iov,
      .msg_iovlen = std::size(iov),
  };

  // 1. Keep sending data until socket is saturated.
  while (sendmsg(fds()[0].get(), &msg, 0) > 0) {
  }

  // 2. Consume one segment of the data.
  EXPECT_EQ(read(fds()[1].get(), memchunk.get(), memlength), memlength) << strerror(errno);

  // 3. Push again 2 packets of <memlength> bytes, observe only one sent.
  EXPECT_EQ(sendmsg(fds()[0].get(), &msg, 0), memlength) << strerror(errno);
}

TEST_P(SocketPair, WaitBeginEnd) {
  fdio_t* io = fdio_unsafe_fd_to_io(fds()[0].get());

  // fdio_unsafe_wait_begin

  zx::handle handle;
  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLIN, handle.reset_and_get_address(), &signals);
    EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLOUT, handle.reset_and_get_address(), &signals);
    EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLRDHUP, handle.reset_and_get_address(), &signals);
    EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLHUP, handle.reset_and_get_address(), &signals);
    EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SIGNAL_NONE);
  }

  // fdio_unsafe_wait_end

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_READABLE, &events);
    EXPECT_EQ(int32_t(events), POLLIN);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_PEER_CLOSED, &events);
    EXPECT_EQ(int32_t(events), POLLIN | POLLRDHUP);
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
    EXPECT_EQ(int32_t(events), POLLOUT);
  }

  fdio_unsafe_release(io);
}

static constexpr ssize_t WRITE_DATA_SIZE = 1024 * 1024;

TEST_P(SocketPair, StreamPartialWrite) {
  if (GetParam() == SOCK_DGRAM) {
    GTEST_SKIP() << "Stream only";
  }

  // Start a thread that reads everything we write.
  auto fut = std::async(
      std::launch::async,
      [](int fd) {
        static char buf[WRITE_DATA_SIZE];
        ssize_t progress = 0;
        while (progress < WRITE_DATA_SIZE) {
          size_t n = WRITE_DATA_SIZE - progress;
          ssize_t status = read(fd, buf, n);
          if (status < 0) {
            return status;
          }
          progress += status;
        }
        return progress;
      },
      fds()[1].get());

  // Write more data than can fit in the socket send buffer.
  static char buf[WRITE_DATA_SIZE];
  size_t progress = 0;
  while (progress < WRITE_DATA_SIZE) {
    size_t n = WRITE_DATA_SIZE - progress;
    ssize_t status = write(fds()[0].get(), buf, n);
    if (status < 0) {
      ASSERT_EQ(errno, EAGAIN) << strerror(errno);
    }
    progress += status;
  }

  // Make sure the other thread read everything.
  ASSERT_EQ(fut.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  ASSERT_EQ(fut.get(), WRITE_DATA_SIZE);
}

INSTANTIATE_TEST_SUITE_P(SocketPair, SocketPair, testing::Values(SOCK_STREAM, SOCK_DGRAM),
                         TypeToString);
INSTANTIATE_TEST_SUITE_P(SocketPairShutdown, SocketPairShutdown,
                         testing::Values(SOCK_STREAM, SOCK_DGRAM), TypeToString);
