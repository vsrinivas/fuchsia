// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <algorithm>

#include <gtest/gtest.h>

sockaddr_in6 MapIpv4SockaddrToIpv6Sockaddr(const sockaddr_in& addr4) {
  sockaddr_in6 addr6 = {
      .sin6_family = AF_INET6,
      .sin6_port = addr4.sin_port,
  };

  // An IPv4-mapped IPv6 address has 8 bytes of zeros, followed by two bytes of ones,
  // followed by the original address.
  addr6.sin6_addr.s6_addr[10] = 0xff;
  addr6.sin6_addr.s6_addr[11] = 0xff;
  memcpy(&addr6.sin6_addr.s6_addr[12], &addr4.sin_addr.s_addr, sizeof(addr4.sin_addr.s_addr));

  char buf[INET6_ADDRSTRLEN];
  EXPECT_TRUE(IN6_IS_ADDR_V4MAPPED(&addr6.sin6_addr))
      << inet_ntop(addr6.sin6_family, &addr6.sin6_addr, buf, sizeof(buf));
  return addr6;
}

sockaddr_in LoopbackSockaddrV4(in_port_t port) {
  return {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr =
          {
              .s_addr = htonl(INADDR_LOOPBACK),
          },
  };
}

sockaddr_in6 LoopbackSockaddrV6(in_port_t port) {
  return {
      .sin6_family = AF_INET6,
      .sin6_port = htons(port),
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };
}

void fill_stream_send_buf(int fd, int peer_fd, ssize_t* out_bytes_written) {
  {
#if defined(__Fuchsia__)
    // In other systems we prefer to get the smallest possible buffer size, but that causes an
    // unnecessarily large amount of writes to fill the send and receive buffers on Fuchsia because
    // of the zircon socket attached to both the sender and the receiver. Each zircon socket will
    // artificially add 256KB (its depth) to the sender's and receiver's buffers.
    //
    // We'll arbitrarily select a larger size which will allow us to fill both zircon sockets
    // faster.
    //
    // TODO(https://fxbug.dev/60337): We can use the minimum buffer size once zircon sockets are not
    // artificially increasing the buffer sizes.
    constexpr int bufsize = 64 << 10;
#else
    // We're about to fill the send buffer; shrink it and the other side's receive buffer to the
    // minimum allowed.
    constexpr int bufsize = 1;
#endif

    EXPECT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)), 0)
        << strerror(errno);
    EXPECT_EQ(setsockopt(peer_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)), 0)
        << strerror(errno);
  }

  int sndbuf_opt;
  socklen_t sndbuf_optlen = sizeof(sndbuf_opt);
  ASSERT_EQ(getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_opt, &sndbuf_optlen), 0)
      << strerror(errno);
  ASSERT_EQ(sndbuf_optlen, sizeof(sndbuf_opt));

  int rcvbuf_opt;
  socklen_t rcvbuf_optlen = sizeof(rcvbuf_opt);
  ASSERT_EQ(getsockopt(peer_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_opt, &rcvbuf_optlen), 0)
      << strerror(errno);
  ASSERT_EQ(rcvbuf_optlen, sizeof(rcvbuf_opt));

  ssize_t total_bytes_written = 0;
#if defined(__linux__)
  // If the send buffer is smaller than the receive buffer, the code below will
  // not work because the first write will not be enough to fill the receive
  // buffer.
  ASSERT_GE(sndbuf_opt, rcvbuf_opt);
  // Write enough bytes at once to fill the receive buffer.
  {
    const std::vector<uint8_t> buf(rcvbuf_opt);
    const ssize_t bytes_written = write(fd, buf.data(), buf.size());
    ASSERT_GE(bytes_written, 0u) << strerror(errno);
    ASSERT_EQ(bytes_written, ssize_t(buf.size()));
    total_bytes_written += bytes_written;
  }

  // Wait for the bytes to be available; afterwards the receive buffer will be full.
  while (true) {
    int available_bytes;
    ASSERT_EQ(ioctl(peer_fd, FIONREAD, &available_bytes), 0) << strerror(errno);
    ASSERT_LE(available_bytes, rcvbuf_opt);
    if (available_bytes == rcvbuf_opt) {
      break;
    }
  }

  // Finally the send buffer can be filled with certainty.
  {
    const std::vector<uint8_t> buf(sndbuf_opt);
    const ssize_t bytes_written = write(fd, buf.data(), buf.size());
    ASSERT_GE(bytes_written, 0u) << strerror(errno);
    ASSERT_EQ(bytes_written, ssize_t(buf.size()));
    total_bytes_written += bytes_written;
  }
#else
  // On Fuchsia, it may take a while for a written packet to land in the netstack's send buffer
  // because of the asynchronous copy from the zircon socket to the send buffer. So we use a small
  // timeout which was empirically tested to ensure no flakiness is introduced.
  timeval original_tv;
  socklen_t tv_len = sizeof(original_tv);
  ASSERT_EQ(getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &original_tv, &tv_len), 0) << strerror(errno);
  ASSERT_EQ(tv_len, sizeof(original_tv));
  const timeval tv = {
      .tv_sec = 0,
      .tv_usec = 1 << 14,  // ~16ms
  };
  ASSERT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)), 0) << strerror(errno);

  const std::vector<uint8_t> buf(sndbuf_opt + rcvbuf_opt);
  // Clocks sometimes jump in infrastructure, which can cause the timeout set above to expire
  // prematurely. Fortunately such jumps are rarely seen in quick succession - if we repeatedly
  // reach the blocking condition we can be reasonably sure that the intended amount of time truly
  // did elapse. Care is taken to reset the counter if data is written, as we are looking for a
  // streak of blocking condition observances.
  for (int i = 0; i < 1 << 6; i++) {
    ssize_t size;
    while ((size = write(fd, buf.data(), buf.size())) > 0) {
      total_bytes_written += size;

      i = 0;
    }
    ASSERT_EQ(size, -1);
    EXPECT_EQ(errno, EAGAIN) << strerror(errno);
  }
  ASSERT_GT(total_bytes_written, 0);
  ASSERT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &original_tv, tv_len), 0) << strerror(errno);
#endif
  if (out_bytes_written != nullptr) {
    *out_bytes_written = total_bytes_written;
  }
}

#if !defined(__Fuchsia__)
fit::deferred_action<std::function<void()>> DisableSigPipe(bool is_write) {
  struct sigaction act = {};
  act.sa_handler = SIG_IGN;
  struct sigaction oldact;
  if (is_write) {
    EXPECT_EQ(sigaction(SIGPIPE, &act, &oldact), 0) << strerror(errno);
  }
  std::function<void()> ret = [=]() {
    if (is_write) {
      EXPECT_EQ(sigaction(SIGPIPE, &oldact, nullptr), 0) << strerror(errno);
    }
  };
  return fit::defer(ret);
}

bool IsRoot() {
  uid_t ruid, euid, suid;
  EXPECT_EQ(getresuid(&ruid, &euid, &suid), 0) << strerror(errno);
  gid_t rgid, egid, sgid;
  EXPECT_EQ(getresgid(&rgid, &egid, &sgid), 0) << strerror(errno);
  auto uids = {ruid, euid, suid};
  auto gids = {rgid, egid, sgid};
  return std::all_of(std::begin(uids), std::end(uids), [](uid_t uid) { return uid == 0; }) &&
         std::all_of(std::begin(gids), std::end(gids), [](gid_t gid) { return gid == 0; });
}
#endif

ssize_t IOMethod::ExecuteIO(const int fd, char* const buf, const size_t len) const {
  // Vectorize the provided buffer into multiple differently-sized iovecs.
  std::vector<iovec> iov;
  {
    char* iov_start = buf;
    size_t len_remaining = len;
    while (len_remaining != 0) {
      const size_t next_len = (len_remaining + 1) / 2;
      iov.push_back({
          .iov_base = iov_start,
          .iov_len = next_len,
      });
      len_remaining -= next_len;
      if (iov_start != nullptr) {
        iov_start += next_len;
      }
    }

    std::uniform_int_distribution<size_t> distr(0, iov.size());
    int seed = testing::UnitTest::GetInstance()->random_seed();
    std::default_random_engine rd(seed);
    iov.insert(iov.begin() + distr(rd), {
                                            .iov_base = buf,
                                            .iov_len = 0,
                                        });
  }

  msghdr msg = {
      .msg_iov = iov.data(),
      // Linux defines `msg_iovlen` as size_t, out of compliance with
      // with POSIX, whereas Fuchsia defines it as int. Bridge the
      // divide using decltype.
      .msg_iovlen = static_cast<decltype(msg.msg_iovlen)>(iov.size()),
  };

  switch (op_) {
    case Op::READ:
      return read(fd, buf, len);
    case Op::READV:
      return readv(fd, iov.data(), static_cast<int>(iov.size()));
    case Op::RECV:
      return recv(fd, buf, len, 0);
    case Op::RECVFROM:
      return recvfrom(fd, buf, len, 0, nullptr, nullptr);
    case Op::RECVMSG:
      return recvmsg(fd, &msg, 0);
    case Op::WRITE:
      return write(fd, buf, len);
    case Op::WRITEV:
      return writev(fd, iov.data(), static_cast<int>(iov.size()));
    case Op::SEND:
      return send(fd, buf, len, 0);
    case Op::SENDTO:
      return sendto(fd, buf, len, 0, nullptr, 0);
    case Op::SENDMSG:
      return sendmsg(fd, &msg, 0);
  }
}

bool IOMethod::isWrite() const {
  switch (op_) {
    case Op::READ:
    case Op::READV:
    case Op::RECV:
    case Op::RECVFROM:
    case Op::RECVMSG:
      return false;
    case Op::WRITE:
    case Op::WRITEV:
    case Op::SEND:
    case Op::SENDTO:
    case Op::SENDMSG:
    default:
      return true;
  }
}

void DoNullPtrIO(const fbl::unique_fd& fd, const fbl::unique_fd& other, IOMethod io_method,
                 bool datagram) {
  // A version of ioMethod::ExecuteIO with special handling for vectorized operations: a 1-byte
  // buffer is prepended to the argument.
  auto ExecuteIO = [io_method](int fd, char* buf, size_t len) {
    char buffer[1];
    iovec iov[] = {
        {
            .iov_base = buffer,
            .iov_len = sizeof(buffer),
        },
        {
            .iov_base = buf,
            .iov_len = len,
        },
    };
    msghdr msg = {
        .msg_iov = iov,
        .msg_iovlen = std::size(iov),
    };

    switch (io_method.Op()) {
      case IOMethod::Op::READ:
      case IOMethod::Op::RECV:
      case IOMethod::Op::RECVFROM:
      case IOMethod::Op::WRITE:
      case IOMethod::Op::SEND:
      case IOMethod::Op::SENDTO:
        return io_method.ExecuteIO(fd, buf, len);
      case IOMethod::Op::READV:
        return readv(fd, iov, std::size(iov));
      case IOMethod::Op::RECVMSG:
        return recvmsg(fd, &msg, 0);
      case IOMethod::Op::WRITEV:
        return writev(fd, iov, std::size(iov));
      case IOMethod::Op::SENDMSG:
        return sendmsg(fd, &msg, 0);
    }
  };

  auto prepareForRead = [&](const char* buf, size_t len) {
    ASSERT_EQ(send(other.get(), buf, len, 0), ssize_t(len)) << strerror(errno);

    // Wait for the packet to arrive since we are nonblocking.
    pollfd pfd = {
        .fd = fd.get(),
        .events = POLLIN,
    };

    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(pfd.revents, POLLIN);
  };

  auto confirmWrite = [&]() {
    char buffer[1];
#if defined(__Fuchsia__)
    if (!datagram) {
      switch (io_method.Op()) {
        case IOMethod::Op::WRITE:
        case IOMethod::Op::SEND:
        case IOMethod::Op::SENDTO:
          break;
        case IOMethod::Op::WRITEV:
        case IOMethod::Op::SENDMSG: {
          // Fuchsia doesn't comply because zircon sockets do not implement atomic vector
          // operations, so these vector operations end up having sent the byte provided in the
          // ExecuteIO closure. See https://fxbug.dev/67928 for more details.
          //
          // Wait for the packet to arrive since we are nonblocking.
          pollfd pfd = {
              .fd = other.get(),
              .events = POLLIN,
          };
          int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
          ASSERT_GE(n, 0) << strerror(errno);
          ASSERT_EQ(n, 1);
          EXPECT_EQ(pfd.revents, POLLIN);
          EXPECT_EQ(recv(other.get(), buffer, sizeof(buffer), 0), 1) << strerror(errno);
          return;
        }
        default:
          FAIL() << "unexpected method " << io_method.IOMethodToString();
      }
    }
#endif
    // Nothing was sent. This is not obvious in the vectorized case.
    EXPECT_EQ(recv(other.get(), buffer, sizeof(buffer), 0), -1);
    EXPECT_EQ(errno, EAGAIN) << strerror(errno);
  };

  // Receive some data so we can attempt to read it below.
  if (!io_method.isWrite()) {
    char buffer[] = {0x74, 0x75};
    prepareForRead(buffer, sizeof(buffer));
  }

  [&]() {
#if defined(__Fuchsia__)
    if (!datagram) {
      switch (io_method.Op()) {
        case IOMethod::Op::READ:
        case IOMethod::Op::RECV:
        case IOMethod::Op::RECVFROM:
        case IOMethod::Op::WRITE:
        case IOMethod::Op::SEND:
        case IOMethod::Op::SENDTO:
          break;

        case IOMethod::Op::READV:
        case IOMethod::Op::RECVMSG:
        case IOMethod::Op::WRITEV:
        case IOMethod::Op::SENDMSG:
          // Fuchsia doesn't comply because zircon sockets do not implement atomic vector
          // operations, so these vector operations report success on the byte provided in the
          // ExecuteIO closure.
          EXPECT_EQ(ExecuteIO(fd.get(), nullptr, 1), 1) << strerror(errno);
          return;
      }
    }
#endif
    EXPECT_EQ(ExecuteIO(fd.get(), nullptr, 1), -1);
    EXPECT_EQ(errno, EFAULT) << strerror(errno);
  }();

  if (io_method.isWrite()) {
    confirmWrite();
  } else {
    char buffer[1];
    auto result = ExecuteIO(fd.get(), buffer, sizeof(buffer));
    if (datagram) {
      // The datagram was consumed in spite of the buffer being null.
      EXPECT_EQ(result, -1);
      EXPECT_EQ(errno, EAGAIN) << strerror(errno);
    } else {
      ssize_t space = sizeof(buffer);
      switch (io_method.Op()) {
        case IOMethod::Op::READV:
        case IOMethod::Op::RECVMSG:
#if defined(__Fuchsia__)
          // Fuchsia consumed one byte above.
#else
          // An additional byte of space was provided in the ExecuteIO closure.
          space += 1;
#endif
          [[fallthrough]];
        case IOMethod::Op::READ:
        case IOMethod::Op::RECV:
        case IOMethod::Op::RECVFROM:
          break;
        default:
          FAIL() << "unexpected method " << io_method.IOMethodToString();
      }
      EXPECT_EQ(result, space) << strerror(errno);
    }
  }

  // Do it again, but this time write less data so that vector operations can work normally.
  if (!io_method.isWrite()) {
    char buffer[] = {0x74};
    prepareForRead(buffer, sizeof(buffer));
  }

  switch (io_method.Op()) {
    case IOMethod::Op::WRITEV:
    case IOMethod::Op::SENDMSG:
#if defined(__Fuchsia__)
      if (!datagram) {
        // Fuchsia doesn't comply because zircon sockets do not implement atomic vector
        // operations, so these vector operations report success on the byte provided in the
        // ExecuteIO closure.
        EXPECT_EQ(ExecuteIO(fd.get(), nullptr, 1), 1) << strerror(errno);
        break;
      }
#endif
      [[fallthrough]];
    case IOMethod::Op::READ:
    case IOMethod::Op::RECV:
    case IOMethod::Op::RECVFROM:
    case IOMethod::Op::WRITE:
    case IOMethod::Op::SEND:
    case IOMethod::Op::SENDTO:
      EXPECT_EQ(ExecuteIO(fd.get(), nullptr, 1), -1);
      EXPECT_EQ(errno, EFAULT) << strerror(errno);
      break;
    case IOMethod::Op::READV:
    case IOMethod::Op::RECVMSG:
      // These vectorized operations never reach the faulty buffer, so they work normally.
      EXPECT_EQ(ExecuteIO(fd.get(), nullptr, 1), 1) << strerror(errno);
      break;
  }

  if (io_method.isWrite()) {
    confirmWrite();
  } else {
    char buffer[1];
    auto result = ExecuteIO(fd.get(), buffer, sizeof(buffer));
    if (datagram) {
      // The datagram was consumed in spite of the buffer being null.
      EXPECT_EQ(result, -1);
      EXPECT_EQ(errno, EAGAIN) << strerror(errno);
    } else {
      switch (io_method.Op()) {
        case IOMethod::Op::READ:
        case IOMethod::Op::RECV:
        case IOMethod::Op::RECVFROM:
          EXPECT_EQ(result, ssize_t(sizeof(buffer))) << strerror(errno);
          break;
        case IOMethod::Op::READV:
        case IOMethod::Op::RECVMSG:
          // The byte we sent was consumed in the ExecuteIO closure.
          EXPECT_EQ(result, -1);
          EXPECT_EQ(errno, EAGAIN) << strerror(errno);
          break;
        default:
          FAIL() << "unexpected method " << io_method.IOMethodToString();
      }
    }
  }
}

ssize_t asyncSocketRead(int recvfd, int sendfd, char* buf, ssize_t len, int flags,
                        sockaddr_in* addr, const socklen_t* addrlen, int socket_type,
                        std::chrono::duration<double> timeout) {
  std::future<ssize_t> recv = std::async(std::launch::async, [recvfd, buf, len, flags]() {
    memset(buf, 0xde, len);
    return recvfrom(recvfd, buf, len, flags, nullptr, nullptr);
  });

  if (recv.wait_for(timeout) == std::future_status::ready) {
    return recv.get();
  }

  // recover the blocked receiver thread
  switch (socket_type) {
    case SOCK_STREAM: {
      // shutdown() would unblock the receiver thread with recv returning 0.
      EXPECT_EQ(shutdown(recvfd, SHUT_RD), 0) << strerror(errno);
      // We do not use 'timeout' because that maybe short here. We expect to succeed and hence use
      // a known large timeout to ensure the test does not hang in case underlying code is broken.
      EXPECT_EQ(recv.wait_for(kTimeout), std::future_status::ready);
      EXPECT_EQ(recv.get(), 0);
      break;
    }
    case SOCK_DGRAM: {
      // Send a 0 length payload to unblock the receiver.
      // This would ensure that the async-task deterministically exits before call to future`s
      // destructor. Calling close(.release()) on recvfd when the async task is blocked on recv(),
      // __does_not__ cause recv to return; this can result in undefined behavior, as the
      // descriptor can get reused. Instead of sending a valid packet to unblock the recv() task,
      // we could call shutdown(), but that returns ENOTCONN (unconnected) but still causing
      // recv() to return. shutdown() becomes unreliable for unconnected UDP sockets because,
      // irrespective of the effect of calling this call, it returns error.
      EXPECT_EQ(sendto(sendfd, nullptr, 0, 0, reinterpret_cast<sockaddr*>(addr), *addrlen), 0)
          << strerror(errno);
      // We use a known large timeout for the same reason as for the above case.
      EXPECT_EQ(recv.wait_for(kTimeout), std::future_status::ready);
      EXPECT_EQ(recv.get(), 0);
      break;
    }
    default: {
      return -1;
    }
  }
  return 0;
}

std::string socketDomainToString(const int domain) {
  switch (domain) {
    case AF_INET:
      return "IPv4";
    case AF_INET6:
      return "IPv6";
    default:
      return std::to_string(domain);
  }
}
