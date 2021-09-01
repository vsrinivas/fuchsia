// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/stdcompat/optional.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

namespace {

class Server final : public fuchsia_io::testing::Node_TestBase {
 public:
  explicit Server(fuchsia_io::wire::NodeInfo describe_info)
      : describe_info_(std::move(describe_info)) {}

  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) override {
    ADD_FAILURE("%s should not be called", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    EXPECT_OK(completer.Reply(ZX_OK).status());
    // FDIO expects the channel to be closed after replying.
    completer.Close(ZX_OK);
  }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    ASSERT_TRUE(describe_info_.has_value(), "Describe called more than once");
    EXPECT_OK(completer.Reply(std::move(*describe_info_)).status());
    describe_info_.reset();
  }

 private:
  cpp17::optional<fuchsia_io::wire::NodeInfo> describe_info_;
};

// Serves |node_info| over |endpoints| using a |Server| instance by
// creating a file descriptor from |client_channel| and immediately closing it.
void ServeAndExerciseFileDescriptionTeardown(fuchsia_io::wire::NodeInfo node_info,
                                             fidl::Endpoints<fuchsia_io::Node> endpoints) {
  Server server(std::move(node_info));
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  fidl::BindServer(loop.dispatcher(), std::move(endpoints.server), &server);
  ASSERT_OK(loop.StartThread("fake-socket-server"));

  {
    fbl::unique_fd fd;
    ASSERT_OK(fdio_fd_create(endpoints.client.channel().release(), fd.reset_and_get_address()));
  }
}

TEST(SocketCleanup, Datagram) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_OK(endpoints.status_value());

  zx::eventpair client_event, server_event;
  ASSERT_OK(zx::eventpair::create(0, &client_event, &server_event));

  fuchsia_io::wire::DatagramSocket dgram_info{.event = std::move(client_event)};
  fuchsia_io::wire::NodeInfo node_info;
  node_info.set_datagram_socket(
      fidl::ObjectView<fuchsia_io::wire::DatagramSocket>::FromExternal(&dgram_info));

  ASSERT_NO_FATAL_FAILURE(
      ServeAndExerciseFileDescriptionTeardown(std::move(node_info), std::move(endpoints.value())));

  // Client must have disposed of its channel and eventpair handle on close.
  EXPECT_STATUS(endpoints->client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                     zx::time::infinite_past(), nullptr),
                ZX_ERR_BAD_HANDLE);
  EXPECT_OK(server_event.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

TEST(SocketCleanup, Stream) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_OK(endpoints.status_value());

  zx::socket client_socket, server_socket;
  ASSERT_OK(zx::socket::create(0, &client_socket, &server_socket));

  fuchsia_io::wire::StreamSocket stream_info{.socket = std::move(client_socket)};
  fuchsia_io::wire::NodeInfo node_info;
  node_info.set_stream_socket(
      fidl::ObjectView<fuchsia_io::wire::StreamSocket>::FromExternal(&stream_info));

  ASSERT_NO_FATAL_FAILURE(
      ServeAndExerciseFileDescriptionTeardown(std::move(node_info), std::move(endpoints.value())));

  // Client must have disposed of its channel and socket handles on close.
  EXPECT_STATUS(endpoints->client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                     zx::time::infinite_past(), nullptr),
                ZX_ERR_BAD_HANDLE);
  EXPECT_OK(server_socket.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

}  // namespace
