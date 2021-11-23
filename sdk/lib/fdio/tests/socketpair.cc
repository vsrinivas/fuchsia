// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains tests for socketpair() behaviors on Fuchsia and on
// host platforms that support this BSD entry point. Tests for Fuchsia or
// fdio specific details should go into fdio_socketpair.cc.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <future>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "sdk/lib/fdio/tests/socketpair_test_helpers.h"

#ifdef __Fuchsia__

#include <lib/zx/thread.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include "src/lib/testing/predicates/status.h"

#endif  // __Fuchsia__

using fdio_tests::SocketPair;
using fdio_tests::TypeToString;

constexpr int kSendFlags =
#if defined(MSG_NOSIGNAL)
    MSG_NOSIGNAL
#else
    0
#endif
    ;

std::thread send_thread(const fbl::unique_fd& fd) {
  return std::thread([&]() {
    std::array<char, 256> buf;

    EXPECT_EQ(send(fd.get(), buf.data(), buf.size(), kSendFlags), -1);
    EXPECT_EQ(errno, EPIPE) << strerror(errno);
  });
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
  EXPECT_EQ(send(fds()[1].get(), buf, sizeof(buf), kSendFlags), -1);
  EXPECT_EQ(errno, EPIPE) << strerror(errno);

  // Or read any more
#ifdef __Fuchsia__
  EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), 0) << strerror(errno);
#else
  // TODO(https://fxbug.dev/79231): On Linux, this returns EAGAIN for datagram sockets.
  if (GetParam() == SOCK_DGRAM) {
    EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), -1);
    EXPECT_EQ(errno, EAGAIN);
  } else {
    EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), 0) << strerror(errno);
  }
#endif
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
  EXPECT_EQ(send(fds()[0].get(), buf, sizeof(buf), kSendFlags), -1);
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
  EXPECT_EQ(send(fds()[0].get(), buf, sizeof(buf), kSendFlags), -1);
  EXPECT_EQ(errno, EPIPE) << strerror(errno);

  // Reading should return no data.
#ifdef __Fuchsia__
  EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), 0) << strerror(errno);
#else
  if (GetParam() == SOCK_DGRAM) {
    // TODO(https://fxbug.dev/79231): On Linux, this returns EAGAIN for datagram sockets.
    EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), -1);
    EXPECT_EQ(errno, EAGAIN) << strerror(errno);
  } else {
    EXPECT_EQ(read(fds()[0].get(), buf, sizeof(buf)), 0) << strerror(errno);
  }
#endif
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

// TODO(https://fxbug.dev/79231): Investigate this failure and either disable on
// Linux and remove this TODO or get it passing and enable it on Linux.
#ifdef __Fuchsia__
#define MAYBE_SelfWritePoll SelfWritePoll
#else  // On Linux, the poll() returns 0 entries with success instead of 1.
#define MAYBE_SelfWritePoll DISABLED_SelfWritePoll
#endif  // __Fuchsia_

TEST_P(SocketPairShutdown, MAYBE_SelfWritePoll) {
  std::thread poll_thread = poll_for_read_with_timeout(fds()[0]);

  EXPECT_EQ(shutdown(fds()[0].get(), SHUT_RDWR), 0) << strerror(errno);

  poll_thread.join();
}

// TODO(https://fxbug.dev/79231): Investigate this failure and either disable on
// Linux and remove this TODO or get it passing and enable it on Linux.
#ifdef __Fuchsia__
#define MAYBE_PeerWritePoll PeerWritePoll
#else  // On Linux, the poll() returns 0 entries with success instead of 1.
#define MAYBE_PeerWritePoll DISABLED_PeerWritePoll
#endif

TEST_P(SocketPairShutdown, MAYBE_PeerWritePoll) {
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

// TODO(https://fxbug.dev/79231): Investigate this failure and either disable on
// Linux and remove this TODO or get it passing and enable it on Linux.
#ifdef __Fuchsia__
#define MAYBE_SelfWriteDuringRecv SelfWriteDuringRecv
#else  // Disabled on Linux. Recv thread hangs instead of exiting
#define MAYBE_SelfWriteDuringRecv DISABLED_SelfWriteDuringRecv
#endif  // __Fuchsia

TEST_P(SocketPair, MAYBE_SelfWriteDuringRecv) {
  std::thread t = recv_thread(fds()[0]);

  EXPECT_EQ(shutdown(fds()[1].get(), SHUT_WR), 0) << strerror(errno);

  t.join();
}

// TODO(https://fxbug.dev/79231): Investigate this failure and either disable on
// Linux and remove this TODO or get it passing and enable it on Linux.
#ifdef __Fuchsia__
#define MAYBE_PeerReadBeforeSend PeerReadBeforeSend
#else  // Disabled on Linux.
#define MAYBE_PeerReadBeforeSend DISABLED_PeerReadBeforeSend
#endif  // __Fuchsia_

TEST_P(SocketPair, MAYBE_PeerReadBeforeSend) {
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

// TODO(https://fxbug.dev/79231): Investigate this failure and either disable on
// Linux and remove this TODO or get it passing and enable it on Linux.
#ifdef __Fuchsia__
#define MAYBE_StreamSendmsgNonblockBoundary StreamSendmsgNonblockBoundary
#else
#define MAYBE_StreamSendmsgNonblockBoundary DISABLED_StreamSendmsgNonblockBoundary
#endif  // __Fuchsia_

// Verify scenario, where multi-segment sendmsg is requested, but the socket has
// just enough spare buffer to *completely* read one segment.
// In this scenario, an attempt to send second segment should immediately fail
// with ZX_ERR_SHOULD_WAIT, but the sendmsg should report first segment length
// rather than failing with EAGAIN.
TEST_P(SocketPair, MAYBE_StreamSendmsgNonblockBoundary) {
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
  while (sendmsg(fds()[0].get(), &msg, kSendFlags) > 0) {
  }

  // 2. Consume one segment of the data.
  EXPECT_EQ(read(fds()[1].get(), memchunk.get(), memlength), memlength) << strerror(errno);

  // 3. Push again 2 packets of <memlength> bytes, observe only one sent.
  EXPECT_EQ(sendmsg(fds()[0].get(), &msg, kSendFlags), memlength) << strerror(errno);
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

// TODO(https://fxbug.dev/79231): These tests depend on Fuchsia specific primitives
// to coordinate between threads. They should be modified to use cross-platform primitives
// and run on other platforms as well, as demonstrated here:
// https://fuchsia-review.googlesource.com/c/fuchsia/+/544785/2..3/sdk/lib/fdio/tests/fdio_socketpair.cc

#ifdef __Fuchsia__

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

#endif  // __Fuchsia__

INSTANTIATE_TEST_SUITE_P(SocketPair, SocketPair, testing::Values(SOCK_STREAM, SOCK_DGRAM),
                         TypeToString);
INSTANTIATE_TEST_SUITE_P(SocketPairShutdown, SocketPairShutdown,
                         testing::Values(SOCK_STREAM, SOCK_DGRAM), TypeToString);
