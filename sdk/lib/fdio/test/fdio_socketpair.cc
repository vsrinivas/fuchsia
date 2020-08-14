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
#include <threads.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <array>
#include <future>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

// TODO(fxbug.dev/44390): Remove once parameterized tests are supported.
// This macro is a cheap workaround, as each test must be uniquely named
// and it's best to have each test exercise a single use case.
#define TEST_P(TestCase, Test)                        \
  TEST(TestCase, Socket##Test) { Test(SOCK_STREAM); } \
  TEST(TestCase, Datagram##Test) { Test(SOCK_DGRAM); }

void SocketpairSetup(std::array<fbl::unique_fd, 2>* fds, uint32_t type) {
  int int_fds[countof(*fds)];
  int status = socketpair(AF_UNIX, type, 0, int_fds);
  ASSERT_EQ(status, 0, "socketpair(AF_UNIX, %u, 0, fds) failed", type);
  (*fds)[0].reset(int_fds[0]);
  (*fds)[1].reset(int_fds[1]);
}

void SocketpairShutdownSetup(std::array<fbl::unique_fd, 2>* fds, uint32_t type) {
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(fds, type));

  // Set both ends to non-blocking to make testing for readability/writability easier.
  ASSERT_EQ(fcntl((*fds)[0].get(), F_SETFL, O_NONBLOCK), 0);
  ASSERT_EQ(fcntl((*fds)[1].get(), F_SETFL, O_NONBLOCK), 0);

  char buf[1] = {};
  // Both sides should be readable.
  errno = 0;
  EXPECT_EQ(read((*fds)[0].get(), buf, sizeof(buf)), -1, "fds[0] should initially be readable");
  EXPECT_EQ(errno, EAGAIN);
  errno = 0;
  EXPECT_EQ(read((*fds)[1].get(), buf, sizeof(buf)), -1, "fds[1] should initially be readable");
  EXPECT_EQ(errno, EAGAIN);

  // Both sides should be writable.
  EXPECT_EQ(write((*fds)[0].get(), buf, sizeof(buf)), 1, "fds[0] should be initially writable");
  EXPECT_EQ(write((*fds)[1].get(), buf, sizeof(buf)), 1, "fds[1] should be initially writable");

  EXPECT_EQ(read((*fds)[0].get(), buf, sizeof(buf)), 1);
  EXPECT_EQ(read((*fds)[1].get(), buf, sizeof(buf)), 1);
}

void Control(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, type));

  // write() and read() should work.
  const char buf[] = "abc";
  ASSERT_EQ(write(fds[0].get(), buf, sizeof(buf)), sizeof(buf), "%s", strerror(errno));

  char recvbuf[sizeof(buf)];
  ASSERT_EQ(read(fds[1].get(), recvbuf, sizeof(recvbuf)), sizeof(buf), "%s", strerror(errno));

  recvbuf[sizeof(recvbuf) - 1] = 0;
  ASSERT_STR_EQ(recvbuf, buf);

  // send() and recv() should also work.
  ASSERT_EQ(send(fds[1].get(), buf, sizeof(buf), 0), sizeof(buf), "%s", strerror(errno));

  ASSERT_EQ(recv(fds[0].get(), recvbuf, sizeof(recvbuf), 0), sizeof(buf), "%s", strerror(errno));

  recvbuf[sizeof(recvbuf) - 1] = 0;
  ASSERT_STR_EQ(recvbuf, buf);
  EXPECT_EQ(close(fds[0].release()), 0, "close(fds[0]) failed");
  EXPECT_EQ(close(fds[1].release()), 0, "close(fds[1]) failed");
}

TEST_P(SocketpairTest, Control)

static_assert(EAGAIN == EWOULDBLOCK, "Assuming EAGAIN and EWOULDBLOCK have same value");

#if defined(__Fuchsia__)
#define SEND_FLAGS 0
#else
#define SEND_FLAGS MSG_NOSIGNAL
#endif

void ShutdownRead(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairShutdownSetup(&fds, type));

  // Write a byte into fds[1] to test for readability later.
  char buf[1] = {};
  EXPECT_EQ(write(fds[1].get(), buf, sizeof(buf)), 1);

  // Close one side down for reading.
  int status = shutdown(fds[0].get(), SHUT_RD);
  EXPECT_EQ(status, 0, "shutdown(fds[0], SHUT_RD)");
  if (status != 0)
    printf("\nerrno %d\n", errno);

  // Can read the byte already written into the pipe.
  EXPECT_EQ(read(fds[0].get(), buf, sizeof(buf)), 1, "fds[0] should not be readable after SHUT_RD");

  // But not send any further bytes
  EXPECT_EQ(send(fds[1].get(), buf, sizeof(buf), SEND_FLAGS), -1);
  EXPECT_EQ(errno, EPIPE, "send should return EPIPE after shutdown(SHUT_RD) on other side");

  // Or read any more
  EXPECT_EQ(read(fds[0].get(), buf, sizeof(buf)), 0);
  EXPECT_EQ(close(fds[0].release()), 0);
  EXPECT_EQ(close(fds[1].release()), 0);
}

TEST_P(SocketpairTest, ShutdownRead)

void ShutdownWrite(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairShutdownSetup(&fds, type));

  // Close one side down for writing.
  int status = shutdown(fds[0].get(), SHUT_WR);
  EXPECT_EQ(status, 0, "shutdown(fds[0], SHUT_WR)");
  if (status != 0)
    printf("\nerrno %d\n", errno);

  char buf[1] = {};

  // Should still be readable.
  EXPECT_EQ(read(fds[0].get(), buf, sizeof(buf)), -1);
  EXPECT_EQ(errno, EAGAIN, "errno after read after SHUT_WR");

  // But not writable
  EXPECT_EQ(send(fds[0].get(), buf, sizeof(buf), SEND_FLAGS), -1, "write after SHUT_WR");
  EXPECT_EQ(errno, EPIPE, "errno after write after SHUT_WR");

  // Should still be able to write + read a message in the other direction.
  EXPECT_EQ(write(fds[1].get(), buf, sizeof(buf)), 1);
  EXPECT_EQ(read(fds[0].get(), buf, sizeof(buf)), 1);
  EXPECT_EQ(close(fds[0].release()), 0);
  EXPECT_EQ(close(fds[1].release()), 0);
}

TEST_P(SocketpairTest, ShutdownWrite)

void ShutdownReadWrite(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairShutdownSetup(&fds, type));

  // Close one side for reading and writing.
  int status = shutdown(fds[0].get(), SHUT_RDWR);
  EXPECT_EQ(status, 0, "shutdown(fds[0], SHUT_RDWR");
  if (status != 0)
    printf("\nerrno %d\n", errno);

  char buf[1] = {};

  // Writing should fail.
  EXPECT_EQ(send(fds[0].get(), buf, sizeof(buf), SEND_FLAGS), -1);
  EXPECT_EQ(errno, EPIPE, "errno after write after SHUT_RDWR");

  // Reading should return no data.
  EXPECT_EQ(read(fds[0].get(), buf, sizeof(buf)), 0);
}

TEST_P(SocketpairTest, ShutdownReadWrite)

typedef struct poll_for_read_args {
  fbl::unique_fd* fd;
  int poll_result;
  zx_time_t poll_time;
} poll_for_read_args_t;

int poll_for_read_with_timeout(void* arg) {
  poll_for_read_args_t* poll_args = (poll_for_read_args_t*)arg;
  struct pollfd pollfd;
  pollfd.fd = poll_args->fd->get();
  pollfd.events = POLLIN;
  pollfd.revents = 0;

  int timeout_ms = 100;
  zx_time_t time_before = zx_clock_get_monotonic();
  poll_args->poll_result = poll(&pollfd, 1, timeout_ms);
  zx_time_t time_after = zx_clock_get_monotonic();
  poll_args->poll_time = time_after - time_before;

  int num_readable = 0;
  EXPECT_EQ(ioctl(poll_args->fd->get(), FIONREAD, &num_readable), 0, "ioctl(FIONREAD)");
  EXPECT_EQ(num_readable, 0);

  return 0;
}

void ShutdownSelfWritePoll(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairShutdownSetup(&fds, type));

  poll_for_read_args_t poll_args = {};
  poll_args.fd = &fds[0];
  thrd_t poll_thread;
  int thrd_create_result = thrd_create(&poll_thread, poll_for_read_with_timeout, &poll_args);
  ASSERT_EQ(thrd_create_result, thrd_success, "create blocking read thread");

  shutdown(fds[0].get(), SHUT_RDWR);

  ASSERT_EQ(thrd_join(poll_thread, NULL), thrd_success, "join blocking read thread");

  EXPECT_EQ(poll_args.poll_result, 1, "poll should have one entry");
  EXPECT_LT(poll_args.poll_time, 100u * 1000 * 1000, "poll should not have timed out");
}

TEST_P(SocketpairTest, ShutdownSelfWritePoll)

void ShutdownPeerWritePoll(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairShutdownSetup(&fds, type));

  poll_for_read_args_t poll_args = {};
  poll_args.fd = &fds[0];
  thrd_t poll_thread;
  int thrd_create_result = thrd_create(&poll_thread, poll_for_read_with_timeout, &poll_args);
  ASSERT_EQ(thrd_create_result, thrd_success, "create blocking read thread");

  shutdown(fds[1].get(), SHUT_RDWR);

  ASSERT_EQ(thrd_join(poll_thread, NULL), thrd_success, "join blocking read thread");

  EXPECT_EQ(poll_args.poll_result, 1, "poll should have one entry");
  EXPECT_LT(poll_args.poll_time, 100u * 1000 * 1000, "poll should not have timed out");
}

TEST_P(SocketpairTest, ShutdownPeerWritePoll)

static constexpr size_t BUF_SIZE = 256;

typedef struct recv_args {
  fbl::unique_fd* fd;
  ssize_t recv_result;
  int recv_errno;
  char buf[BUF_SIZE];
} recv_args_t;

int recv_thread(void* arg) {
  recv_args_t* recv_args = (recv_args_t*)arg;

  recv_args->recv_result = recv(recv_args->fd->get(), recv_args->buf, BUF_SIZE, 0u);
  if (recv_args->recv_result < 0)
    recv_args->recv_errno = errno;

  return 0;
}

void ShutdownSelfReadDuringRecv(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, type));

  recv_args_t recv_args = {};
  recv_args.fd = &fds[0];
  thrd_t t;
  int thrd_create_result = thrd_create(&t, recv_thread, &recv_args);
  ASSERT_EQ(thrd_create_result, thrd_success, "create blocking read thread");

  shutdown(fds[0].get(), SHUT_RD);

  ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking read thread");

  EXPECT_EQ(recv_args.recv_result, 0, "recv should have returned 0");
  EXPECT_EQ(recv_args.recv_errno, 0, "recv should have left errno alone");
}

TEST_P(SocketpairTest, ShutdownSelfReadDuringRecv)

void ShutdownSelfWriteDuringRecv(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, type));

  recv_args_t recv_args = {};
  recv_args.fd = &fds[0];
  thrd_t t;
  int thrd_create_result = thrd_create(&t, recv_thread, &recv_args);
  ASSERT_EQ(thrd_create_result, thrd_success, "create blocking read thread");

  shutdown(fds[1].get(), SHUT_WR);

  ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking read thread");

  EXPECT_EQ(recv_args.recv_result, 0, "recv should have returned 0");
  EXPECT_EQ(recv_args.recv_errno, 0, "recv should have left errno alone");
}

TEST_P(SocketpairTest, ShutdownSelfWriteDuringRecv)

typedef struct send_args {
  fbl::unique_fd* fd;
  ssize_t send_result;
  int send_errno;
  char buf[BUF_SIZE];
} send_args_t;

int send_thread(void* arg) {
  send_args_t* send_args = (send_args_t*)arg;

  send_args->send_result = send(send_args->fd->get(), send_args->buf, BUF_SIZE, 0u);
  if (send_args->send_result < 0)
    send_args->send_errno = errno;

  return 0;
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

void ShutdownSelfWriteDuringSend(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, type));

  // First, fill up the socket so the next send() will block.
  char buf[BUF_SIZE] = {};
  while (true) {
    ssize_t status = send(fds[0].get(), buf, sizeof(buf), MSG_DONTWAIT);
    if (status < 0) {
      ASSERT_EQ(errno, EAGAIN, "send should eventually return EAGAIN when full");
      break;
    }
  }
  send_args_t send_args = {};
  send_args.fd = &fds[0];
  thrd_t t;
  // Then start a thread blocking on a send().
  int thrd_create_result = thrd_create(&t, send_thread, &send_args);
  ASSERT_EQ(thrd_create_result, thrd_success, "create blocking send thread");

  // Wait for the thread to sleep in send.
  ASSERT_OK(
      WaitForState(*(zx::unowned_thread(thrd_get_zx_handle(t))), ZX_THREAD_STATE_BLOCKED_WAIT_ONE));

  shutdown(fds[0].get(), SHUT_WR);

  ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking send thread");

  EXPECT_EQ(send_args.send_result, -1, "send should have returned -1");
  EXPECT_EQ(send_args.send_errno, EPIPE, "send should have set errno to EPIPE");
}

TEST_P(SocketpairTest, ShutdownSelfWriteDuringSend)

void ShutdownSelfWriteBeforeSend(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, type));

  // First, fill up the socket so the next send() will block.
  char buf[BUF_SIZE] = {};
  while (true) {
    ssize_t status = send(fds[0].get(), buf, sizeof(buf), MSG_DONTWAIT);
    if (status < 0) {
      ASSERT_EQ(errno, EAGAIN, "send should eventually return EAGAIN when full");
      break;
    }
  }
  send_args_t send_args = {};
  send_args.fd = &fds[0];
  thrd_t t;

  shutdown(fds[0].get(), SHUT_WR);

  // Then start a thread blocking on a send().
  int thrd_create_result = thrd_create(&t, send_thread, &send_args);
  ASSERT_EQ(thrd_create_result, thrd_success, "create blocking send thread");

  ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking send thread");

  EXPECT_EQ(send_args.send_result, -1, "send should have returned -1");
  EXPECT_EQ(send_args.send_errno, EPIPE, "send should have set errno to EPIPE");
}

TEST_P(SocketpairTest, ShutdownSelfWriteBeforeSend)

void ShutdownPeerReadDuringSend(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, type));

  // First, fill up the socket so the next send() will block.
  char buf[BUF_SIZE] = {};
  while (true) {
    ssize_t status = send(fds[0].get(), buf, sizeof(buf), MSG_DONTWAIT);
    if (status < 0) {
      ASSERT_EQ(errno, EAGAIN, "send should eventually return EAGAIN when full");
      break;
    }
  }
  send_args_t send_args = {};
  send_args.fd = &fds[0];
  thrd_t t;
  int thrd_create_result = thrd_create(&t, send_thread, &send_args);
  ASSERT_EQ(thrd_create_result, thrd_success, "create blocking send thread");

  // Wait for the thread to sleep in send.
  ASSERT_OK(
      WaitForState(*(zx::unowned_thread(thrd_get_zx_handle(t))), ZX_THREAD_STATE_BLOCKED_WAIT_ONE));

  shutdown(fds[1].get(), SHUT_RD);

  ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking send thread");

  EXPECT_EQ(send_args.send_result, -1, "send should have returned -1");
  EXPECT_EQ(send_args.send_errno, EPIPE, "send should have set errno to EPIPE");
}

TEST_P(SocketpairTest, ShutdownPeerReadDuringSend)

void ShutdownPeerReadBeforeSend(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, type));

  // First, fill up the socket so the next send() will block.
  char buf[BUF_SIZE] = {};
  while (true) {
    ssize_t status = send(fds[0].get(), buf, sizeof(buf), MSG_DONTWAIT);
    if (status < 0) {
      ASSERT_EQ(errno, EAGAIN, "send should eventually return EAGAIN when full");
      break;
    }
  }

  shutdown(fds[1].get(), SHUT_RD);

  send_args_t send_args = {};
  send_args.fd = &fds[0];
  thrd_t t;
  int thrd_create_result = thrd_create(&t, send_thread, &send_args);
  ASSERT_EQ(thrd_create_result, thrd_success, "create blocking send thread");

  ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking send thread");

  EXPECT_EQ(send_args.send_result, -1, "send should have returned -1");
  EXPECT_EQ(send_args.send_errno, EPIPE, "send should have set errno to EPIPE");
}

TEST_P(SocketpairTest, ShutdownPeerReadBeforeSend)

void CloneOrUnwrapAndWrap(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, type));

  zx::handle handle;
  int status = fdio_fd_clone(fds[0].get(), handle.reset_and_get_address());
  ASSERT_OK(status, "fdio_fd_clone() failed");

  fbl::unique_fd cloned_fd;
  status = fdio_fd_create(handle.release(), cloned_fd.reset_and_get_address());
  EXPECT_EQ(status, 0, "fdio_fd_create(..., &cloned_fd) failed");

  status = fdio_fd_transfer(fds[0].release(), handle.reset_and_get_address());
  ASSERT_OK(status, "fdio_fd_transfer() failed");

  fbl::unique_fd transferred_fd;
  status = fdio_fd_create(handle.release(), transferred_fd.reset_and_get_address());
  EXPECT_EQ(status, 0, "fdio_fd_create(..., &transferred_fd) failed");

  // Verify that an operation specific to socketpairs works on these fds.
  ASSERT_EQ(0, shutdown(transferred_fd.get(), SHUT_WR), "shutdown(transferred_fd, SHUT_WR) failed");
  ASSERT_EQ(0, shutdown(cloned_fd.get(), SHUT_RD), "shutdown(cloned_fd, SHUT_RD) failed");
}

TEST_P(SocketpairTest, CloneOrUnwrapAndWrap)

// Verify scenario, where multi-segment recvmsg is requested, but the socket has
// just enough data to *completely* fill one segment.
// In this scenario, an attempt to read data for the next segment immediately
// fails with ZX_ERR_SHOULD_WAIT; at this point recvmsg should report total
// number of bytes read, instead of failing with EAGAIN.
TEST(SocketpairTest, StreamRecvmsgNonblockBoundary) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, SOCK_STREAM));

  ASSERT_EQ(fcntl(fds[0].get(), F_SETFL, O_NONBLOCK), 0);
  ASSERT_EQ(fcntl(fds[1].get(), F_SETFL, O_NONBLOCK), 0);

  // Write 4 bytes of data to socket.
  size_t actual;
  const uint32_t data_out = 0x12345678;
  EXPECT_EQ((ssize_t)sizeof(data_out), write(fds[0].get(), &data_out, sizeof(data_out)),
            "Socket write failed");

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

  struct msghdr msg;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = iov;
  msg.msg_iovlen = sizeof(iov) / sizeof(*iov);
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  actual = recvmsg(fds[1].get(), &msg, 0);
  EXPECT_EQ(sizeof(data_in1), actual, "Socket read failed");
}

// Verify scenario, where multi-segment sendmsg is requested, but the socket has
// just enough spare buffer to *completely* read one segment.
// In this scenario, an attempt to send second segment should immediately fail
// with ZX_ERR_SHOULD_WAIT, but the sendmsg should report first segment length
// rather than failing with EAGAIN.
TEST(SocketpairTest, StreamSendmsgNonblockBoundary) {
  const ssize_t memlength = 65536;
  void* memchunk = malloc(memlength);

  struct iovec iov[2];
  iov[0].iov_base = memchunk;
  iov[0].iov_len = memlength;
  iov[1].iov_base = memchunk;
  iov[1].iov_len = memlength;

  std::array<fbl::unique_fd, 2> fds;
  SocketpairSetup(&fds, SOCK_STREAM);

  ASSERT_EQ(fcntl(fds[0].get(), F_SETFL, O_NONBLOCK), 0);
  ASSERT_EQ(fcntl(fds[1].get(), F_SETFL, O_NONBLOCK), 0);

  struct msghdr msg;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = iov;
  msg.msg_iovlen = sizeof(iov) / sizeof(*iov);
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  // 1. Keep sending data until socket is saturated.
  while (sendmsg(fds[0].get(), &msg, 0) > 0)
    ;

  // 2. Consume one segment of the data.
  EXPECT_EQ(memlength, read(fds[1].get(), memchunk, memlength), "Socket read failed.");

  // 3. Push again 2 packets of <memlength> bytes, observe only one sent.
  EXPECT_EQ(memlength, sendmsg(fds[0].get(), &msg, 0),
            "Partial sendmsg failed; is the socket buffer varying?");

  free(memchunk);
}

void WaitBeginEnd(uint32_t type) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, type));

  fdio_t* io = fdio_unsafe_fd_to_io(fds[0].get());

  // fdio_unsafe_wait_begin

  zx::handle handle;
  zx_signals_t signals;
  fdio_unsafe_wait_begin(io, POLLIN, handle.reset_and_get_address(), &signals);
  EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
  EXPECT_EQ(signals, ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED,
            "");

  fdio_unsafe_wait_begin(io, POLLOUT, handle.reset_and_get_address(), &signals);
  EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
  EXPECT_EQ(signals, ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED);

  signals = ZX_SIGNAL_NONE;
  fdio_unsafe_wait_begin(io, POLLRDHUP, handle.reset_and_get_address(), &signals);
  EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
  EXPECT_EQ(signals, ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED);

  // fdio_unsafe_wait_end

  uint32_t events = 0u;
  fdio_unsafe_wait_end(io, ZX_SOCKET_READABLE, &events);
  EXPECT_EQ(events, (uint32_t)POLLIN);

  events = 0u;
  fdio_unsafe_wait_end(io, ZX_SOCKET_PEER_CLOSED, &events);
  EXPECT_EQ(events, (uint32_t)(POLLIN | POLLRDHUP));

  events = 0u;
  fdio_unsafe_wait_end(io, ZX_SOCKET_PEER_WRITE_DISABLED, &events);
  EXPECT_EQ(events, (uint32_t)(POLLIN | POLLRDHUP));

  events = 0u;
  fdio_unsafe_wait_end(io, ZX_SOCKET_WRITABLE, &events);
  EXPECT_EQ(events, (uint32_t)POLLOUT);

  events = 0u;
  fdio_unsafe_wait_end(io, ZX_SOCKET_WRITE_DISABLED, &events);
  EXPECT_EQ(events, (uint32_t)POLLOUT);

  fdio_unsafe_release(io);
}

TEST_P(SocketpairTest, WaitBeginEnd)

static constexpr ssize_t WRITE_DATA_SIZE = 1024 * 1024;

TEST(SocketpairTest, StreamPartialWrite) {
  std::array<fbl::unique_fd, 2> fds;
  ASSERT_NO_FATAL_FAILURES(SocketpairSetup(&fds, SOCK_STREAM));

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
      fds[1].get());

  // Write more data than can fit in the socket send buffer.
  static char buf[WRITE_DATA_SIZE];
  size_t progress = 0;
  while (progress < WRITE_DATA_SIZE) {
    size_t n = WRITE_DATA_SIZE - progress;
    ssize_t status = write(fds[0].get(), buf, n);
    if (status < 0) {
      ASSERT_EQ(errno, EAGAIN, "%s", strerror(errno));
    }
    progress += status;
  }

  // Make sure the other thread read everything.
  ASSERT_EQ(fut.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  ASSERT_EQ(fut.get(), WRITE_DATA_SIZE, "other thread did not read everything");
}

#undef TEST_P
