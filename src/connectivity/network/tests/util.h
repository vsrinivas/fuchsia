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

constexpr char kFastUdpEnvVar[] = "FAST_UDP";

struct SocketDomain {
  // Should only be used when switching on the return value of which(), because
  // enum classes don't guarantee type-safe construction.
  enum class Which : sa_family_t {
    IPv4 = AF_INET,
    IPv6 = AF_INET6,
  };
  constexpr static SocketDomain IPv4() { return SocketDomain(Which::IPv4); }
  constexpr static SocketDomain IPv6() { return SocketDomain(Which::IPv6); }
  sa_family_t Get() const { return static_cast<sa_family_t>(which_); }

  Which which() const { return which_; }

 private:
  explicit constexpr SocketDomain(Which which) : which_(which) {}
  Which which_;
};

struct SocketType {
  // Should only be used when switching on the return value of which(), because
  // enum classes don't guarantee type-safe construction.
  enum class Which : int {
    Stream = SOCK_STREAM,
    Dgram = SOCK_DGRAM,
  };
  constexpr static SocketType Stream() { return SocketType(Which::Stream); }
  constexpr static SocketType Dgram() { return SocketType(Which::Dgram); }
  int Get() const { return static_cast<int>(which_); }

  Which which() const { return which_; }

 private:
  explicit constexpr SocketType(Which which) : which_(which) {}
  Which which_;
};

struct ShutdownType {
  // Should only be used when switching on the return value of which(), because
  // enum classes don't guarantee type-safe construction.
  enum class Which : int {
    Read = SHUT_RD,
    Write = SHUT_WR,
  };
  constexpr static ShutdownType Read() { return ShutdownType(Which::Read); }
  constexpr static ShutdownType Write() { return ShutdownType(Which::Write); }
  int Get() const { return static_cast<int>(which_); }

  Which which() const { return which_; }

 private:
  explicit constexpr ShutdownType(Which which) : which_(which) {}
  Which which_;
};

// Returns a `sockaddr_in6` address mapped from the provided `sockaddr_in`.
sockaddr_in6 MapIpv4SockaddrToIpv6Sockaddr(const sockaddr_in& addr4);

#if defined(__Fuchsia__)
#include <zircon/syscalls/object.h>
// Returns the socket info associated with the provided `fd`, backed by a
// `fposix_socket::StreamSocket`.
void ZxSocketInfoStream(int fd, zx_info_socket_t& out_info);

// Returns the socket info associated with the provided `fd`, backed by a
// `fposix_socket::DatagramSocket`.
void ZxSocketInfoDgram(int fd, zx_info_socket_t& out_info);
#endif

// Returns the Tx capacity of the provided `fd`. NOTE: On Fuchsia, this accounts for
// buffer space available within kernel primitives.
void TxCapacity(int fd, size_t& out_capacity);

// Returns the Rx capacity of the provided `fd`. NOTE: On Fuchsia, this accounts for
// buffer space available within kernel primitives.
void RxCapacity(int fd, size_t& out_capacity);

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

// DisableSigPipe is typically invoked on Linux, in cases where the caller
// expects to perform stream socket writes on an unconnected socket. In such
// cases, SIGPIPE is expected on Linux. This returns a fit::deferred_action object
// whose destructor would undo the signal masking performed here.
//
// send{,to,msg} support the MSG_NOSIGNAL flag to suppress this behaviour, but
// write and writev do not.
fit::deferred_action<std::function<void()>> DisableSigPipe(bool is_write);

// Returns a sockaddr_in holding an IPv4 loopback address with the provided port.
sockaddr_in LoopbackSockaddrV4(in_port_t port);

// Returns a sockaddr_in6 holding an IPv6 loopback address with the provided port.
sockaddr_in6 LoopbackSockaddrV6(in_port_t port);

// Fills `fd`'s send buffer and writes the number of bytes written to `out_bytes_written`.
//
// Assumes that `fd` was previously connected to `peer_fd`.
void fill_stream_send_buf(int fd, int peer_fd, ssize_t* out_bytes_written);

class VectorizedIOMethod {
 public:
  enum class Op {
    READV,
    RECVMSG,
    WRITEV,
    SENDMSG,
  };

  explicit constexpr VectorizedIOMethod(Op op) : op_(op) {}
  Op Op() const { return op_; }

  ssize_t ExecuteIO(int fd, iovec* iovecs, size_t len) const;

  constexpr const char* IOMethodToString() const {
    switch (op_) {
      case Op::READV:
        return "Readv";
      case Op::RECVMSG:
        return "Recvmsg";
      case Op::WRITEV:
        return "Writev";
      case Op::SENDMSG:
        return "Sendmsg";
    }
  }

 private:
  const enum Op op_;
};

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
        return VectorizedIOMethod(VectorizedIOMethod::Op::READV).IOMethodToString();
      case Op::RECV:
        return "Recv";
      case Op::RECVFROM:
        return "Recvfrom";
      case Op::RECVMSG:
        return VectorizedIOMethod(VectorizedIOMethod::Op::RECVMSG).IOMethodToString();
      case Op::WRITE:
        return "Write";
      case Op::WRITEV:
        return VectorizedIOMethod(VectorizedIOMethod::Op::WRITEV).IOMethodToString();
      case Op::SEND:
        return "Send";
      case Op::SENDTO:
        return "Sendto";
      case Op::SENDMSG:
        return VectorizedIOMethod(VectorizedIOMethod::Op::SENDMSG).IOMethodToString();
    }
  }

 private:
  const enum Op op_;
};

constexpr std::initializer_list<IOMethod> kRecvIOMethods = {
    IOMethod::Op::READ,     IOMethod::Op::READV,   IOMethod::Op::RECV,
    IOMethod::Op::RECVFROM, IOMethod::Op::RECVMSG,
};

constexpr std::initializer_list<IOMethod> kSendIOMethods = {
    IOMethod::Op::WRITE,  IOMethod::Op::WRITEV,  IOMethod::Op::SEND,
    IOMethod::Op::SENDTO, IOMethod::Op::SENDMSG,
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
                        SocketType socket_type, SocketDomain socket_domain,
                        std::chrono::duration<double> timeout);

// Returns a human-readable string representing the provided domain.
constexpr std::string_view socketDomainToString(const SocketDomain& domain) {
  switch (domain.which()) {
    case SocketDomain::Which::IPv4:
      return "IPv4";
    case SocketDomain::Which::IPv6:
      return "IPv6";
  }
}

// Returns a human-readable string representing the provided socket type.
constexpr std::string_view socketTypeToString(const SocketType& socket_type) {
  switch (socket_type.which()) {
    case SocketType::Which::Dgram:
      return "Datagram";
    case SocketType::Which::Stream:
      return "Stream";
  }
}

// Returns a sockaddr and its length holding the loopback address for the provided
// socket domain.
std::pair<sockaddr_storage, socklen_t> LoopbackSockaddrAndSocklenForDomain(
    const SocketDomain& domain);

#endif  // SRC_CONNECTIVITY_NETWORK_TESTS_UTIL_H_
