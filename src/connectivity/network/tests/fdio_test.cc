// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure the zircon libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/spawn.h>
#include <lib/sync/completion.h>
#include <lib/zx/process.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <array>
#include <thread>
#include <type_traits>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"
#include "util.h"

TEST(NetStreamTest, ReleasePortNoClose) {
  fbl::unique_fd first;
  ASSERT_TRUE(first = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr =
          {
              .s_addr = htonl(INADDR_LOOPBACK),
          },
  };

  ASSERT_EQ(bind(first.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(first.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  fbl::unique_fd second;
  ASSERT_TRUE(second = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  // Confirm bind fails while address is in use.
  ASSERT_EQ(bind(second.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), -1);
  ASSERT_EQ(errno, EADDRINUSE) << strerror(errno);

  // Simulate unexpected process exit by closing the handle without calling close.
  zx::handle handle;
  ASSERT_OK(fdio_fd_transfer(first.release(), handle.reset_and_get_address()));
  ASSERT_OK(zx_handle_close(handle.release()));

  while (true) {
    int ret = bind(second.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    if (ret == 0) {
      break;
    }
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(errno, EADDRINUSE) << strerror(errno);

    zx::nanosleep(zx::deadline_after(zx::msec(20)));
  }
}

TEST(NetStreamTest, RaceClose) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client_end;
  ASSERT_OK(fdio_fd_transfer(fd.release(), client_end.channel().reset_and_get_address()));
  fidl::WireSyncClient client{std::move(client_end)};

  sync_completion_t completion;

  std::array<std::thread, 10> workers;
  for (auto& worker : workers) {
    worker = std::thread([&client, &completion]() {
      ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));

      auto response = client->Close();
      if (zx_status_t status = response.status(); status != ZX_OK) {
        EXPECT_STATUS(status, ZX_ERR_PEER_CLOSED);
      } else {
        EXPECT_TRUE(response->is_ok()) << zx_status_get_string(response.value().error_value());
      }
    });
  }

  sync_completion_signal(&completion);

  std::for_each(workers.begin(), workers.end(), std::mem_fn(&std::thread::join));
}

TEST(SocketTest, ZXSocketSignalNotPermitted) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client_end;
  ASSERT_OK(fdio_fd_transfer(fd.release(), client_end.channel().reset_and_get_address()));
  fidl::WireSyncClient client{std::move(client_end)};

  const fidl::WireResult result = client->Describe();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_TRUE(response.has_socket());
  const zx::socket& socket = response.socket();

  EXPECT_STATUS(socket.signal(ZX_USER_SIGNAL_0, 0), ZX_ERR_ACCESS_DENIED);
  EXPECT_STATUS(socket.signal(0, ZX_USER_SIGNAL_0), ZX_ERR_ACCESS_DENIED);
  EXPECT_STATUS(socket.signal_peer(ZX_USER_SIGNAL_0, 0), ZX_ERR_ACCESS_DENIED);
  EXPECT_STATUS(socket.signal_peer(0, ZX_USER_SIGNAL_0), ZX_ERR_ACCESS_DENIED);
}

static std::optional<zx::socket> stream_handle(
    fidl::WireResponse<typename fuchsia_posix_socket::StreamSocket::Describe> response) {
  if (response.has_socket()) {
    return std::move(response.socket());
  };
  return {};
}

static std::optional<zx::socket> datagram_handle(
    fidl::WireResponse<typename fuchsia_posix_socket::DatagramSocket::Describe> response) {
  if (response.has_socket()) {
    return std::move(response.socket());
  };
  return {};
}

static std::optional<zx::eventpair> synchronous_datagram_handle(
    fidl::WireResponse<typename fuchsia_posix_socket::SynchronousDatagramSocket::Describe>
        response) {
  if (response.has_event()) {
    return std::move(response.event());
  };
  return {};
}

template <int Type, int NetworkProtocol, typename FidlProtocol, typename HandleType,
          std::optional<HandleType> (*GetHandle)(
              typename fidl::WireResponse<typename FidlProtocol::Describe>),
          zx_signals_t PeerClosed>
struct SocketImpl {
  using Client = fidl::WireSyncClient<FidlProtocol>;
  using ClientEnd = fidl::ClientEnd<FidlProtocol>;
  using Handle = HandleType;

  static int type() { return Type; }
  static int network_protocol() { return NetworkProtocol; }
  static std::optional<Handle> handle(
      typename fidl::WireResponse<typename FidlProtocol::Describe> response) {
    return GetHandle(response);
  }
  static zx_signals_t peer_closed() { return PeerClosed; }
};

using StreamSocketImpl = SocketImpl<SOCK_STREAM, IPPROTO_IP, fuchsia_posix_socket::StreamSocket,
                                    zx::socket, stream_handle, ZX_SOCKET_PEER_CLOSED>;

using SynchronousDatagramSocketImplIcmp =
    SocketImpl<SOCK_DGRAM, IPPROTO_ICMP, fuchsia_posix_socket::SynchronousDatagramSocket,
               zx::eventpair, synchronous_datagram_handle, ZX_EVENTPAIR_PEER_CLOSED>;

using SynchronousDatagramSocketImplIp =
    SocketImpl<SOCK_DGRAM, IPPROTO_IP, fuchsia_posix_socket::SynchronousDatagramSocket,
               zx::eventpair, synchronous_datagram_handle, ZX_EVENTPAIR_PEER_CLOSED>;

using DatagramSocketImpl = SocketImpl<SOCK_DGRAM, IPPROTO_IP, fuchsia_posix_socket::DatagramSocket,
                                      zx::socket, datagram_handle, ZX_SOCKET_PEER_CLOSED>;

template <typename Impl>
class SocketTest : public testing::Test {
 protected:
  void SetUp() override {
    constexpr char kFastUdpEnvVar[] = "FAST_UDP";
    if (std::getenv(kFastUdpEnvVar)) {
      if (std::is_same<Impl, SynchronousDatagramSocketImplIp>::value) {
        GTEST_SKIP()
            << "SynchronousDatagramSocket protocol should not be used for SOCK_DGRAM sockets when "
               "fast UDP is enabled.";
      }
    } else {
      if (std::is_same<Impl, DatagramSocketImpl>::value) {
        GTEST_SKIP()
            << "DatagramSocket protocol should not be used for SOCK_DGRAM sockets when fast UDP is "
               "not enabled.";
      }
    }
    ASSERT_TRUE(fd_ = fbl::unique_fd(socket(AF_INET, Impl::type(), Impl::network_protocol())))
        << strerror(errno);
    addr_ = {
        .sin_family = AF_INET,
        .sin_addr =
            {
                .s_addr = htonl(INADDR_ANY),
            },
    };
    ASSERT_EQ(bind(fd_.get(), reinterpret_cast<const struct sockaddr*>(&addr_), sizeof(addr_)), 0)
        << strerror(errno);
    socklen_t addrlen = sizeof(addr_);
    ASSERT_EQ(getsockname(fd_.get(), reinterpret_cast<struct sockaddr*>(&addr_), &addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(addr_));
  }

  const fbl::unique_fd& fd() { return fd_; }
  fbl::unique_fd& mutable_fd() { return fd_; }

 private:
  fbl::unique_fd fd_;
  struct sockaddr_in addr_;
};

class SocketTestNames {
 public:
  template <typename T>
  static std::string GetName(int i) {
    if (std::is_same<T, StreamSocketImpl>())
      return "Stream" + testing::PrintToString(i);
    if (std::is_same<T, DatagramSocketImpl>())
      return "Datagram" + testing::PrintToString(i);
    if (std::is_same<T, SynchronousDatagramSocketImplIp>())
      return "SynchronousDatagramIp" + testing::PrintToString(i);
    if (std::is_same<T, SynchronousDatagramSocketImplIcmp>())
      return "SynchronousDatagramIcmp" + testing::PrintToString(i);
  }
};

using SocketTypes =
    testing::Types<StreamSocketImpl, DatagramSocketImpl, SynchronousDatagramSocketImplIp,
                   SynchronousDatagramSocketImplIcmp>;
TYPED_TEST_SUITE(SocketTest, SocketTypes, SocketTestNames);

TYPED_TEST(SocketTest, CloseResourcesOnClose) {
  // Increase the reference count.
  zx::channel clone;
  ASSERT_OK(fdio_fd_clone(this->fd().get(), clone.reset_and_get_address()));

  typename TypeParam::Client client;
  typename TypeParam::ClientEnd client_end;
  ASSERT_OK(
      fdio_fd_transfer(this->mutable_fd().release(), client_end.channel().reset_and_get_address()));
  client.Bind(std::move(client_end));

  const fidl::WireResult describe_result = client->Describe();
  ASSERT_OK(describe_result.status());
  const fidl::WireResponse describe_response = describe_result.value();
  std::optional handle_opt = TypeParam::handle(describe_response);
  ASSERT_TRUE(handle_opt.has_value());
  typename TypeParam::Handle& handle = handle_opt.value();

  zx_signals_t observed;

  ASSERT_STATUS(
      handle.wait_one(TypeParam::peer_closed(), zx::deadline_after(zx::msec(100)), &observed),
      ZX_ERR_TIMED_OUT);

  auto close_response = client->Close();
  EXPECT_OK(close_response.status());
  EXPECT_TRUE(close_response->is_ok()) << zx_status_get_string(close_response->error_value());

  // We still have `clone`, nothing should be closed yet.
  ASSERT_STATUS(
      handle.wait_one(TypeParam::peer_closed(), zx::deadline_after(zx::msec(100)), &observed),
      ZX_ERR_TIMED_OUT);

  clone.reset();

  // Give a generous timeout for closures; the channel closing is inherently asynchronous with
  // respect to the `Close` FIDL call above (since its return must come over the channel). The
  // handle closure is not inherently asynchronous, but happens to be as an implementation detail.
  zx::time deadline = zx::deadline_after(zx::sec(5));
  ASSERT_OK(handle.wait_one(TypeParam::peer_closed(), deadline, &observed));
  ASSERT_OK(client.client_end().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, deadline, &observed));
}

TEST(SocketTest, AcceptedSocketIsConnected) {
  // Create the listening endpoint (server).
  fbl::unique_fd serverfd;
  ASSERT_TRUE(serverfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr =
          {
              .s_addr = htonl(INADDR_LOOPBACK),
          },
  };
  ASSERT_EQ(bind(serverfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(listen(serverfd.get(), 1), 0) << strerror(errno);

  // Get the address the server is listening on.
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(serverfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  // Connect to the listening endpoint (client).
  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
            0)
      << strerror(errno);

  // Accept the new connection (client) on the listening endpoint (server).
  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(serverfd.get(), nullptr, nullptr))) << strerror(errno);
  ASSERT_EQ(close(serverfd.release()), 0) << strerror(errno);

  fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client_end;
  ASSERT_OK(fdio_fd_transfer(connfd.release(), client_end.channel().reset_and_get_address()));
  fidl::WireSyncClient client{std::move(client_end)};

  const fidl::WireResult result = client->Describe();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_TRUE(response.has_socket());
  const zx::socket& socket = response.socket();

  zx_signals_t pending;
  ASSERT_STATUS(socket.wait_one(0, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending, ZX_SOCKET_WRITABLE | ZX_USER_SIGNAL_3);
}

TEST(SocketTest, CloseClonedSocketAfterTcpRst) {
  // Create the listening endpoint (server).
  fbl::unique_fd serverfd;
  ASSERT_TRUE(serverfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr =
          {
              .s_addr = htonl(INADDR_LOOPBACK),
          },
  };
  ASSERT_EQ(bind(serverfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(listen(serverfd.get(), 1), 0) << strerror(errno);

  // Get the address the server is listening on.
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(serverfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  // Connect to the listening endpoint (client).
  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
            0)
      << strerror(errno);

  // Accept the new connection (client) on the listening endpoint (server).
  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(serverfd.get(), nullptr, nullptr))) << strerror(errno);
  ASSERT_EQ(close(serverfd.release()), 0) << strerror(errno);

  // Clone the file descriptor a bunch of times to increase the refcount.
  std::array<fbl::unique_fd, 10> connfds;
  for (auto& clonefd : connfds) {
    {
      zx::channel channel;
      ASSERT_OK(fdio_fd_clone(connfd.get(), channel.reset_and_get_address()));
      ASSERT_OK(fdio_fd_create(channel.release(), clonefd.reset_and_get_address()));
    }
  }

  // Fill up the rcvbuf (client-side).
  ASSERT_NO_FATAL_FAILURE(fill_stream_send_buf(connfd.get(), clientfd.get(), nullptr));

  // Closing the client-side connection while it has data that has not been
  // read by the client should trigger a TCP RST.
  ASSERT_EQ(close(clientfd.release()), 0) << strerror(errno);

  std::array<pollfd, connfds.size()> pfds;
  for (size_t i = 0; i < connfds.size(); ++i) {
    pfds[i] = {
        .fd = connfds[i].get(),
        .events = POLLOUT,
    };
  }
  int n = poll(pfds.data(), pfds.size(), std::chrono::milliseconds(kTimeout).count());
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(static_cast<size_t>(n), pfds.size());
  for (auto const& pfd : pfds) {
    ASSERT_EQ(pfd.revents, POLLOUT | POLLERR | POLLHUP);
  }

  // Now that the socket's endpoint has been closed, clone the socket again to increase the
  // endpoint's reference count, then close all copies of the socket.
  std::array<fidl::WireSyncClient<fuchsia_posix_socket::StreamSocket>, 10> clients;
  for (auto& client : clients) {
    fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client_end;
    ASSERT_OK(fdio_fd_clone(connfd.get(), client_end.channel().reset_and_get_address()));
    client.Bind(std::move(client_end));
  }

  for (auto& client : clients) {
    auto response = client->Close();
    EXPECT_OK(response.status());
    EXPECT_TRUE(response->is_ok()) << zx_status_get_string(response.value().error_value());
  }

  ASSERT_EQ(close(connfd.release()), 0) << strerror(errno);
  for (auto& fd : connfds) {
    ASSERT_EQ(close(fd.release()), 0) << strerror(errno);
  }
}

TEST(SocketTest, PassFD) {
  fbl::unique_fd listener;
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr_in = {
      .sin_family = AF_INET,
      .sin_addr =
          {
              .s_addr = htonl(INADDR_LOOPBACK),
          },
  };
  auto addr = reinterpret_cast<struct sockaddr*>(&addr_in);
  socklen_t addr_len = sizeof(addr_in);

  ASSERT_EQ(bind(listener.get(), addr, addr_len), 0) << strerror(errno);
  {
    socklen_t addr_len_in = addr_len;
    ASSERT_EQ(getsockname(listener.get(), addr, &addr_len), 0) << strerror(errno);
    EXPECT_EQ(addr_len, addr_len_in);
  }
  ASSERT_EQ(listen(listener.get(), 1), 0) << strerror(errno);

  zx::process proc;
  {
    fbl::unique_fd client;
    ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
    ASSERT_EQ(connect(client.get(), addr, addr_len), 0) << strerror(errno);

    std::array<fdio_spawn_action_t, 2> actions = {
        fdio_spawn_action_t{
            .action = FDIO_SPAWN_ACTION_CLONE_FD,
            .fd =
                {
                    .local_fd = client.get(),
                    .target_fd = STDIN_FILENO,
                },
        },
        fdio_spawn_action_t{
            .action = FDIO_SPAWN_ACTION_CLONE_FD,
            .fd =
                {
                    .local_fd = client.get(),
                    .target_fd = STDOUT_FILENO,
                },
        },
    };

    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH] = {};
    constexpr char bin_path[] = "/pkg/bin/cat";
    const char* argv[] = {bin_path, nullptr};

    ASSERT_OK(fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO,
                             bin_path, argv, nullptr, actions.size(), actions.data(),
                             proc.reset_and_get_address(), err_msg))
        << err_msg;

    ASSERT_EQ(close(client.release()), 0) << strerror(errno);
  }

  fbl::unique_fd conn;
  ASSERT_TRUE(conn = fbl::unique_fd(accept(listener.get(), nullptr, nullptr))) << strerror(errno);

  constexpr char out[] = "hello";
  ASSERT_EQ(write(conn.get(), out, sizeof(out)), (ssize_t)sizeof(out)) << strerror(errno);
  ASSERT_EQ(shutdown(conn.get(), SHUT_WR), 0) << strerror(errno);

  ASSERT_OK(proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr));

  char in[sizeof(out) + 1];
  ASSERT_EQ(read(conn.get(), in, sizeof(in)), (ssize_t)sizeof(out)) << strerror(errno);
  ASSERT_STREQ(in, out);
}
