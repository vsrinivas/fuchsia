// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-async/cpp/bind.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

class Server final : public llcpp::fuchsia::io::testing::Node_TestBase {
 public:
  explicit Server(llcpp::fuchsia::io::wire::NodeInfo describe_info)
      : describe_info_(std::move(describe_info)) {}

  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) override {
    ADD_FAILURE("%s should not be called", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  using Interface = llcpp::fuchsia::io::Node::Interface;

  void Close(Interface::CloseCompleter::Sync& completer) override {
    EXPECT_OK(completer.Reply(ZX_OK).status());
    // FDIO expects the channel to be closed after replying.
    completer.Close(ZX_OK);
  }

  void Describe(Interface::DescribeCompleter::Sync& completer) override {
    ASSERT_TRUE(describe_info_.has_value(), "Describe called more than once");
    EXPECT_OK(completer.Reply(std::move(*describe_info_)).status());
    describe_info_.reset();
  }

 private:
  fit::optional<llcpp::fuchsia::io::wire::NodeInfo> describe_info_;
};

// Serves |node_info| over |server_channel| to |client_channel| using a |Server| instance by
// creating a file descriptor from |client_channel| and immediately closing it.
void ServeAndExerciseFileDescriptionTeardown(llcpp::fuchsia::io::wire::NodeInfo node_info,
                                             zx::channel client_channel,
                                             zx::channel server_channel) {
  Server server(std::move(node_info));
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto bind_result = fidl::BindServer(loop.dispatcher(), std::move(server_channel), &server);
  ASSERT_TRUE(bind_result.is_ok(), "failed to bind server: %s",
              zx_status_get_string(bind_result.error()));
  ASSERT_OK(loop.StartThread("fake-socket-server"));

  {
    fbl::unique_fd fd;
    ASSERT_OK(fdio_fd_create(client_channel.release(), fd.reset_and_get_address()));
  }
}

TEST(SocketCleanup, Datagram) {
  zx::channel client_channel, server_channel;
  ASSERT_OK(zx::channel::create(0, &client_channel, &server_channel));

  zx::eventpair client_event, server_event;
  ASSERT_OK(zx::eventpair::create(0, &client_event, &server_event));

  llcpp::fuchsia::io::wire::DatagramSocket dgram_info{.event = std::move(client_event)};
  llcpp::fuchsia::io::wire::NodeInfo node_info;
  node_info.set_datagram_socket(fidl::unowned_ptr(&dgram_info));

  zx::unowned_channel client_handle(client_channel);
  ASSERT_NO_FATAL_FAILURES(ServeAndExerciseFileDescriptionTeardown(
      std::move(node_info), std::move(client_channel), std::move(server_channel)));

  // Client must have disposed of its channel and eventpair handle on close.
  EXPECT_STATUS(client_handle->wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(), nullptr),
                ZX_ERR_BAD_HANDLE);
  EXPECT_OK(server_event.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

TEST(SocketCleanup, Stream) {
  zx::channel client_channel, server_channel;
  ASSERT_OK(zx::channel::create(0, &client_channel, &server_channel));

  zx::socket client_socket, server_socket;
  ASSERT_OK(zx::socket::create(0, &client_socket, &server_socket));

  llcpp::fuchsia::io::wire::StreamSocket stream_info{.socket = std::move(client_socket)};
  llcpp::fuchsia::io::wire::NodeInfo node_info;
  node_info.set_stream_socket(fidl::unowned_ptr(&stream_info));

  zx::unowned_channel client_handle(client_channel);
  ASSERT_NO_FATAL_FAILURES(ServeAndExerciseFileDescriptionTeardown(
      std::move(node_info), std::move(client_channel), std::move(server_channel)));

  // Client must have disposed of its channel and socket handles on close.
  EXPECT_STATUS(client_handle->wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(), nullptr),
                ZX_ERR_BAD_HANDLE);
  EXPECT_OK(server_socket.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

}  // namespace
