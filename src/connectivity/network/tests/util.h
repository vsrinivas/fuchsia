// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_

#include <lib/fit/defer.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

#include <chrono>
#include <functional>
#include <future>
#include <random>

#include <fbl/unique_fd.h>

constexpr std::chrono::duration kTimeout = std::chrono::seconds(10);

// Returns a `sockaddr_in6` address mapped from the provided `sockaddr_in`.
sockaddr_in6 MapIpv4SockaddrToIpv6Sockaddr(const sockaddr_in& addr4);

template <typename T>
void AssertBlocked(const std::future<T>& fut) {
  // Give an asynchronous blocking operation some time to reach the blocking state. Clocks
  // sometimes jump in infrastructure, which may cause a single wait to trip sooner than expected,
  // without the asynchronous task getting a meaningful shot at running. We protect against that by
  // splitting the wait into multiple calls as an attempt to guarantee that clock jumps do not
  // impact the duration of a wait.
  for (int i = 0; i < 50; i++) {
    ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(1)), std::future_status::timeout);
  }
}

#if !defined(__Fuchsia__)
// DisableSigPipe is typically invoked on Linux, in cases where the caller
// expects to perform stream socket writes on an unconnected socket. In such
// cases, SIGPIPE is expected on Linux. This returns a fit::deferred_action object
// whose destructor would undo the signal masking performed here.
//
// send{,to,msg} support the MSG_NOSIGNAL flag to suppress this behaviour, but
// write and writev do not.
fit::deferred_action<std::function<void()>> DisableSigPipe(bool is_write);

// Returns whether the current process has root privileges.
bool IsRoot();
#endif

// Returns a sockaddr_in holding an IPv4 loopback address with the provided port.
sockaddr_in LoopbackSockaddrV4(in_port_t port);

// Returns a sockaddr_in6 holding an IPv6 loopback address with the provided port.
sockaddr_in6 LoopbackSockaddrV6(in_port_t port);

// Fills `fd`'s send buffer and writes the number of bytes written to `out_bytes_written`.
//
// Assumes that `fd` was previously connected to `peer_fd`.
void fill_stream_send_buf(int fd, int peer_fd, ssize_t* out_bytes_written);

class IOMethod {
 public:
  enum class Op {
    READ,
    READV,
    RECV,
    RECVFROM,
    RECVMSG,
    WRITE,
    WRITEV,
    SEND,
    SENDTO,
    SENDMSG,
  };

  constexpr IOMethod(Op op) : op_(op) {}
  Op Op() const { return op_; }

  ssize_t ExecuteIO(int fd, char* buf, size_t len) const;

  bool isWrite() const;

  constexpr const char* IOMethodToString() const {
    switch (op_) {
      case Op::READ:
        return "Read";
      case Op::READV:
        return "Readv";
      case Op::RECV:
        return "Recv";
      case Op::RECVFROM:
        return "Recvfrom";
      case Op::RECVMSG:
        return "Recvmsg";
      case Op::WRITE:
        return "Write";
      case Op::WRITEV:
        return "Writev";
      case Op::SEND:
        return "Send";
      case Op::SENDTO:
        return "Sendto";
      case Op::SENDMSG:
        return "Sendmsg";
    }
  }

 private:
  const enum Op op_;
};

constexpr std::initializer_list<IOMethod> kRecvIOMethods = {
    IOMethod::Op::READ,     IOMethod::Op::READV,   IOMethod::Op::RECV,
    IOMethod::Op::RECVFROM, IOMethod::Op::RECVMSG,
};

constexpr std::initializer_list<IOMethod> kAllIOMethods = {
    IOMethod::Op::READ,    IOMethod::Op::READV,   IOMethod::Op::RECV,   IOMethod::Op::RECVFROM,
    IOMethod::Op::RECVMSG, IOMethod::Op::WRITE,   IOMethod::Op::WRITEV, IOMethod::Op::SEND,
    IOMethod::Op::SENDTO,  IOMethod::Op::SENDMSG,
};

// Performs I/O between `fd` and `other` using `io_method` with a null buffer.
void DoNullPtrIO(const fbl::unique_fd& fd, const fbl::unique_fd& other, IOMethod io_method,
                 bool datagram);

// Use this routine to test blocking socket reads. On failure, this attempts to recover the
// blocked thread. Return value:
//      (1) actual length of read data on successful recv
//      (2) 0, when we abort a blocked recv
//      (3) -1, on failure of both of the above operations.
ssize_t asyncSocketRead(int recvfd, int sendfd, char* buf, ssize_t len, int flags,
                        sockaddr_in* addr, const socklen_t* addrlen, int socket_type,
                        std::chrono::duration<double> timeout);

// Returns a human-readable string representing the provided domain.
std::string socketDomainToString(int domain);

#endif  // SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_
