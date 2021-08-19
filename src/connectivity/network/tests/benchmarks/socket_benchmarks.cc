// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <lib/syslog/cpp/macros.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <fbl/unique_fd.h>
#include <perftest/perftest.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace {

#define CHECK_TRUE_ERRNO(true_condition) FX_CHECK(true_condition) << strerror(errno)
#define CHECK_ZERO_ERRNO(zero_condition) CHECK_TRUE_ERRNO((zero_condition) == 0)

template <typename T>
class AddrStorage {
 public:
  static_assert(std::is_same_v<T, sockaddr_in> || std::is_same_v<T, sockaddr_in6>);
  sockaddr* as_sockaddr() { return reinterpret_cast<sockaddr*>(&addr); }
  socklen_t socklen() const { return sizeof(addr); }
  T addr;
};

class Ipv6 {
 public:
  using SockAddr = AddrStorage<sockaddr_in6>;
  static constexpr int kFamily = AF_INET6;

  static SockAddr loopback() {
    return {
        .addr =
            {
                .sin6_family = kFamily,
                .sin6_addr = IN6ADDR_LOOPBACK_INIT,
            },
    };
  }
};

class Ipv4 {
 public:
  using SockAddr = AddrStorage<sockaddr_in>;
  static constexpr int kFamily = AF_INET;

  static SockAddr loopback() {
    return {
        .addr =
            {
                .sin_family = kFamily,
                .sin_addr =
                    {
                        .s_addr = htonl(INADDR_LOOPBACK),
                    },
            },
    };
  }
};

// Helper no-op function to assert functions abstracted over IP version are properly parameterized.
template <typename Ip>
void TemplateIsIpVersion() {
  static_assert(std::is_same_v<Ip, Ipv4> || std::is_same_v<Ip, Ipv6>);
}

// Tests the unidirectional throughput of streaming `transfer` bytes on a TCP loopback socket.
//
// Measures the time to write `transfer` bytes on one end of the socket and read them on the other
// end on the same thread and calculates the throughput.
template <typename Ip>
bool TcpWriteRead(perftest::RepeatState* state, size_t transfer) {
  TemplateIsIpVersion<Ip>();
  using Addr = typename Ip::SockAddr;
  fbl::unique_fd listen_sock;
  CHECK_TRUE_ERRNO(listen_sock = fbl::unique_fd(socket(Ip::kFamily, SOCK_STREAM, 0)));
  Addr sockaddr = Ip::loopback();
  CHECK_ZERO_ERRNO(bind(listen_sock.get(), sockaddr.as_sockaddr(), sockaddr.socklen()));
  CHECK_ZERO_ERRNO(listen(listen_sock.get(), 0));

  socklen_t socklen = sockaddr.socklen();
  CHECK_ZERO_ERRNO(getsockname(listen_sock.get(), sockaddr.as_sockaddr(), &socklen));

  fbl::unique_fd client_sock;
  CHECK_TRUE_ERRNO(client_sock = fbl::unique_fd(socket(Ip::kFamily, SOCK_STREAM, 0)));

  // Set send buffer to transfer size to ensure we can write `transfer` bytes before reading it on
  // the other end.
  FX_CHECK(transfer < std::numeric_limits<int32_t>::max());
  int32_t sndbuf = static_cast<int32_t>(transfer);
  CHECK_ZERO_ERRNO(setsockopt(client_sock.get(), SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)));
  // Disable the Nagle algorithm, it introduces artificial latency that defeats this test.
  const int32_t no_delay = 1;
  CHECK_ZERO_ERRNO(
      setsockopt(client_sock.get(), SOL_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)));

  CHECK_ZERO_ERRNO(connect(client_sock.get(), sockaddr.as_sockaddr(), sockaddr.socklen()));

  fbl::unique_fd server_sock;
  CHECK_TRUE_ERRNO(server_sock = fbl::unique_fd(accept(listen_sock.get(), nullptr, nullptr)));

  std::vector<uint8_t> send_bytes, recv_bytes;
  // Avoid large memory regions with zeroes that can cause the system to try and reclaim pages from
  // us. For more information see Zircon page scanner and eviction strategies.
  send_bytes.resize(transfer, 0xAA);
  recv_bytes.resize(transfer, 0xBB);

  state->SetBytesProcessedPerRun(transfer);
  while (state->KeepRunning()) {
    for (size_t sent = 0; sent < transfer;) {
      ssize_t wr = write(client_sock.get(), send_bytes.data() + sent, transfer - sent);
      CHECK_TRUE_ERRNO(wr > 0);
      sent += wr;
    }
    for (size_t recv = 0; recv < transfer;) {
      ssize_t rd = read(server_sock.get(), recv_bytes.data() + recv, transfer - recv);
      CHECK_TRUE_ERRNO(rd > 0);
      recv += rd;
    }
  }

  return true;
}

// Tests the unidirectional throughput of transmitting a message of `size` bytes over a UDP
// loopback socket.
//
// Measures the time to write a message with `message_size` bytes on one end of the socket and read
// it on the other on the same thread and calculates the throughput.
template <typename Ip>
bool UdpWriteRead(perftest::RepeatState* state, size_t message_size) {
  TemplateIsIpVersion<Ip>();
  using Addr = typename Ip::SockAddr;
  fbl::unique_fd server_sock;
  CHECK_TRUE_ERRNO(server_sock = fbl::unique_fd(socket(Ip::kFamily, SOCK_DGRAM, 0)));
  Addr sockaddr = Ip::loopback();
  CHECK_ZERO_ERRNO(bind(server_sock.get(), sockaddr.as_sockaddr(), sockaddr.socklen()));

  socklen_t socklen = sockaddr.socklen();
  CHECK_ZERO_ERRNO(getsockname(server_sock.get(), sockaddr.as_sockaddr(), &socklen));

  fbl::unique_fd client_sock;
  CHECK_TRUE_ERRNO(client_sock = fbl::unique_fd(socket(Ip::kFamily, SOCK_DGRAM, 0)));
  CHECK_ZERO_ERRNO(connect(client_sock.get(), sockaddr.as_sockaddr(), sockaddr.socklen()));

  std::vector<uint8_t> send_bytes, recv_bytes;
  // Avoid large memory regions with zeroes that can cause the system to try and reclaim pages from
  // us. For more information see Zircon page scanner and eviction strategies.
  send_bytes.resize(message_size, 0xAA);
  recv_bytes.resize(message_size, 0xBB);

  state->SetBytesProcessedPerRun(message_size);
  while (state->KeepRunning()) {
    ssize_t wr = write(client_sock.get(), send_bytes.data(), send_bytes.size());
    CHECK_TRUE_ERRNO(wr > 0);
    FX_CHECK(static_cast<size_t>(wr) == send_bytes.size())
        << "wrote " << wr << " expected " << send_bytes.size();

    ssize_t rd = read(server_sock.get(), recv_bytes.data(), recv_bytes.size());
    CHECK_TRUE_ERRNO(rd > 0);
    FX_CHECK(static_cast<size_t>(rd) == recv_bytes.size())
        << "read " << rd << " expected " << send_bytes.size();
  }

  return true;
}

void RegisterTests() {
  constexpr char kTestNameFmt[] = "WriteRead/%s/%s/%ld%s";
  enum class Transport { kUdp, kTcp };
  enum class Network { kIpv4, kIpv6 };

  auto get_test_name = [&kTestNameFmt](Transport transport, Network network,
                                       size_t bytes) -> std::string {
    const char* unit = "B";
    if (bytes >= 1024) {
      bytes /= 1024;
      unit = "kB";
    }
    const char* transport_name = [transport]() {
      switch (transport) {
        case Transport::kUdp:
          return "UDP";
        case Transport::kTcp:
          return "TCP";
      }
    }();
    const char* network_name = [network]() {
      switch (network) {
        case Network::kIpv4:
          return "IPv4";
        case Network::kIpv6:
          return "IPv6";
      }
    }();

    return fxl::StringPrintf(kTestNameFmt, transport_name, network_name, bytes, unit);
  };

  constexpr size_t kTransferSizesForTcp[] = {
      1 << 10, 10 << 10, 100 << 10, 500 << 10, 1000 << 10,
  };
  for (size_t transfer : kTransferSizesForTcp) {
    perftest::RegisterTest(get_test_name(Transport::kTcp, Network::kIpv4, transfer).c_str(),
                           TcpWriteRead<Ipv4>, transfer);
    perftest::RegisterTest(get_test_name(Transport::kTcp, Network::kIpv6, transfer).c_str(),
                           TcpWriteRead<Ipv6>, transfer);
  }

  // NB: Knowledge encoded at a distance here. This should not be hitting IP fragmentation but
  // Netstack does not support the IP_DONTFRAG flag or equivalent.
  constexpr size_t kMessageSizesForUdp[] = {1, 100, 1 << 10, 10 << 10, 60 << 10};
  for (size_t message_size : kMessageSizesForUdp) {
    perftest::RegisterTest(get_test_name(Transport::kUdp, Network::kIpv4, message_size).c_str(),
                           UdpWriteRead<Ipv4>, message_size);
    perftest::RegisterTest(get_test_name(Transport::kUdp, Network::kIpv6, message_size).c_str(),
                           UdpWriteRead<Ipv6>, message_size);
  }
}
PERFTEST_CTOR(RegisterTests);
}  // namespace

int main(int argc, char** argv) {
  constexpr char kTestSuiteName[] = "fuchsia.network.socket.loopback";
  return perftest::PerfTestMain(argc, argv, kTestSuiteName);
}
