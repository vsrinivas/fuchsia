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

#include <array>
#include <thread>

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

  fidl::WireSyncClient<fuchsia_posix_socket::StreamSocket> client;
  ASSERT_OK(fdio_fd_transfer(fd.release(), client.mutable_channel()->reset_and_get_address()));

  sync_completion_t completion;

  std::array<std::thread, 10> workers;
  for (auto& worker : workers) {
    worker = std::thread([&client, &completion]() {
      ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));

      auto response = client.Close();
      if (zx_status_t status = response.status(); status != ZX_OK) {
        EXPECT_STATUS(status, ZX_ERR_PEER_CLOSED);
      } else {
        EXPECT_OK(response.Unwrap()->s);
      }
    });
  }

  sync_completion_signal(&completion);

  std::for_each(workers.begin(), workers.end(), std::mem_fn(&std::thread::join));
}

TEST(SocketTest, ZXSocketSignalNotPermitted) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  fidl::WireSyncClient<fuchsia_posix_socket::StreamSocket> client;
  ASSERT_OK(fdio_fd_transfer(fd.release(), client.mutable_channel()->reset_and_get_address()));

  auto response = client.Describe();
  ASSERT_OK(response.status());
  const fuchsia_io::wire::NodeInfo& node_info = response.Unwrap()->info;
  ASSERT_EQ(node_info.which(), fuchsia_io::wire::NodeInfo::Tag::kStreamSocket);

  const zx::socket& socket = node_info.stream_socket().socket;

  EXPECT_STATUS(socket.signal(ZX_USER_SIGNAL_0, 0), ZX_ERR_ACCESS_DENIED);
  EXPECT_STATUS(socket.signal(0, ZX_USER_SIGNAL_0), ZX_ERR_ACCESS_DENIED);
  EXPECT_STATUS(socket.signal_peer(ZX_USER_SIGNAL_0, 0), ZX_ERR_ACCESS_DENIED);
  EXPECT_STATUS(socket.signal_peer(0, ZX_USER_SIGNAL_0), ZX_ERR_ACCESS_DENIED);
}

static const zx::socket& stream_handle(const fuchsia_io::wire::NodeInfo& node_info) {
  return node_info.stream_socket().socket;
}

static const zx::eventpair& datagram_handle(const fuchsia_io::wire::NodeInfo& node_info) {
  return node_info.datagram_socket().event;
}

template <int Type, typename ClientType, fuchsia_io::wire::NodeInfo::Tag Tag, typename HandleType,
          const HandleType& (*GetHandle)(const fuchsia_io::wire::NodeInfo& node_info),
          zx_signals_t PeerClosed>
struct SocketImpl {
  using Client = ClientType;
  using Handle = HandleType;

  static int type() { return Type; };
  static fuchsia_io::wire::NodeInfo::Tag tag() { return Tag; }
  static const Handle& handle(const fuchsia_io::wire::NodeInfo& node_info) {
    return GetHandle(node_info);
  }
  static zx_signals_t peer_closed() { return PeerClosed; };
};

template <typename Impl>
class SocketTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(fd_ = fbl::unique_fd(socket(AF_INET, Impl::type(), 0))) << strerror(errno);
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

using StreamSocketImpl =
    SocketImpl<SOCK_STREAM, fidl::WireSyncClient<fuchsia_posix_socket::StreamSocket>,
               fuchsia_io::wire::NodeInfo::Tag::kStreamSocket, zx::socket, stream_handle,
               ZX_SOCKET_PEER_CLOSED>;

using DatagramSocketImpl =
    SocketImpl<SOCK_DGRAM, fidl::WireSyncClient<fuchsia_posix_socket::DatagramSocket>,
               fuchsia_io::wire::NodeInfo::Tag::kDatagramSocket, zx::eventpair, datagram_handle,
               ZX_EVENTPAIR_PEER_CLOSED>;

class SocketTestNames {
 public:
  template <typename T>
  static std::string GetName(int i) {
    if (std::is_same<T, StreamSocketImpl>())
      return "Stream" + testing::PrintToString(i);
    if (std::is_same<T, DatagramSocketImpl>())
      return "Datagram" + testing::PrintToString(i);
  }
};

using SocketTypes = testing::Types<StreamSocketImpl, DatagramSocketImpl>;
TYPED_TEST_SUITE(SocketTest, SocketTypes, SocketTestNames);

TYPED_TEST(SocketTest, CloseResourcesOnClose) {
  // Increase the reference count.
  zx::channel clone;
  ASSERT_OK(fdio_fd_clone(this->fd().get(), clone.reset_and_get_address()));

  typename TypeParam::Client client;
  ASSERT_OK(fdio_fd_transfer(this->mutable_fd().release(),
                             client.mutable_channel()->reset_and_get_address()));

  auto describe_response = client.Describe();
  ASSERT_OK(describe_response.status());
  const fuchsia_io::wire::NodeInfo& node_info = describe_response.Unwrap()->info;
  ASSERT_EQ(node_info.which(), TypeParam::tag());

  zx_signals_t observed;

  ASSERT_STATUS(TypeParam::handle(node_info).wait_one(TypeParam::peer_closed(),
                                                      zx::deadline_after(zx::msec(100)), &observed),
                ZX_ERR_TIMED_OUT);

  auto close_response = client.Close();
  EXPECT_OK(close_response.status());
  EXPECT_OK(close_response.Unwrap()->s);

  // We still have `clone`, nothing should be closed yet.
  ASSERT_STATUS(TypeParam::handle(node_info).wait_one(TypeParam::peer_closed(),
                                                      zx::deadline_after(zx::msec(100)), &observed),
                ZX_ERR_TIMED_OUT);

  clone.reset();

  // Give a generous timeout for closures; the channel closing is inherently asynchronous with
  // respect to the `Close` FIDL call above (since its return must come over the channel). The
  // handle closure is not inherently asynchronous, but happens to be as an implementation detail.
  zx::time deadline = zx::deadline_after(zx::sec(5));
  ASSERT_OK(TypeParam::handle(node_info).wait_one(TypeParam::peer_closed(), deadline, &observed));
  ASSERT_OK(client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, deadline, &observed));
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

  fidl::WireSyncClient<fuchsia_posix_socket::StreamSocket> client;
  ASSERT_OK(fdio_fd_transfer(connfd.release(), client.mutable_channel()->reset_and_get_address()));

  auto response = client.Describe();
  ASSERT_OK(response.status());
  const fuchsia_io::wire::NodeInfo& node_info = response.Unwrap()->info;
  ASSERT_EQ(node_info.which(), fuchsia_io::wire::NodeInfo::Tag::kStreamSocket);

  const zx::socket& socket = node_info.stream_socket().socket;

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
  fill_stream_send_buf(connfd.get(), clientfd.get());

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
    ASSERT_OK(fdio_fd_clone(connfd.get(), client.mutable_channel()->reset_and_get_address()));
  }

  for (auto& client : clients) {
    auto response = client.Close();
    EXPECT_OK(response.status());
    EXPECT_OK(response.Unwrap()->s);
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
    constexpr char bin_path[] = "/bin/cat";
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
