// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-async/cpp/bind.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

template <typename Protocol>
class Server final : public fidl::testing::WireTestBase<Protocol> {
 public:
  using DescribeResponse = typename fidl::WireResponse<typename Protocol::Describe>;

  Server(const char* protocol, DescribeResponse response)
      : protocol_(protocol), describe_response_(std::move(response)) {}

 private:
  using Super = fidl::testing::WireTestBase<Protocol>;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE("%s should not be called", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(typename Super::CloseCompleter::Sync& completer) final {
    completer.ReplySuccess();
    EXPECT_OK(completer.result_of_reply().status());
    // FDIO expects the channel to be closed after replying.
    completer.Close(ZX_OK);
  }

  void Query(typename Super::QueryCompleter::Sync& completer) final {
    uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(protocol_.data()));
    completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, protocol_.size()));
  }

  void Describe(typename Super::DescribeCompleter::Sync& completer) final {
    ASSERT_TRUE(describe_response_.has_value(), "Describe called more than once");
    completer.Reply(std::exchange(describe_response_, std::nullopt).value());
    EXPECT_OK(completer.result_of_reply().status());
  }

  std::string_view protocol_;
  std::optional<DescribeResponse> describe_response_;
};

// Serves |node_info| over |endpoints| using a |Server| instance by
// creating a file descriptor from |client_channel| and immediately closing it.
template <typename Protocol>
void ServeAndExerciseFileDescriptionTeardown(
    const char* protocol, typename Server<Protocol>::DescribeResponse describe_response,
    fidl::Endpoints<Protocol> endpoints) {
  Server<Protocol> server(protocol, describe_response);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  fidl::BindServer(loop.dispatcher(), std::move(endpoints.server), &server);
  ASSERT_OK(loop.StartThread("fake-socket-server"));

  {
    fbl::unique_fd fd;
    ASSERT_OK(fdio_fd_create(endpoints.client.channel().release(), fd.reset_and_get_address()));
  }
}

TEST(SocketCleanup, SynchronousDatagram) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_posix_socket::SynchronousDatagramSocket>();
  ASSERT_OK(endpoints.status_value());

  zx::eventpair client_event, server_event;
  ASSERT_OK(zx::eventpair::create(0, &client_event, &server_event));

  fidl::Arena alloc;
  ASSERT_NO_FATAL_FAILURE(ServeAndExerciseFileDescriptionTeardown(
      fuchsia_posix_socket::wire::kSynchronousDatagramSocketProtocolName,
      fidl::WireResponse<fuchsia_posix_socket::SynchronousDatagramSocket::Describe>{
          fuchsia_posix_socket::wire::SynchronousDatagramSocketDescribeResponse::Builder(alloc)
              .event(std::move(client_event))
              .Build()},
      std::move(endpoints.value())));

  // Client must have disposed of its channel and eventpair handle on close.
  EXPECT_STATUS(endpoints->client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                     zx::time::infinite_past(), nullptr),
                ZX_ERR_BAD_HANDLE);
  EXPECT_OK(server_event.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

TEST(SocketCleanup, Stream) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_posix_socket::StreamSocket>();
  ASSERT_OK(endpoints.status_value());

  zx::socket client_socket, server_socket;
  ASSERT_OK(zx::socket::create(0, &client_socket, &server_socket));

  fidl::Arena alloc;
  ASSERT_NO_FATAL_FAILURE(ServeAndExerciseFileDescriptionTeardown(
      fuchsia_posix_socket::wire::kStreamSocketProtocolName,
      fidl::WireResponse<fuchsia_posix_socket::StreamSocket::Describe>{
          fuchsia_posix_socket::wire::StreamSocketDescribeResponse::Builder(alloc)
              .socket(std::move(client_socket))
              .Build()},
      std::move(endpoints.value())));

  // Client must have disposed of its channel and socket handles on close.
  EXPECT_STATUS(endpoints->client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                     zx::time::infinite_past(), nullptr),
                ZX_ERR_BAD_HANDLE);
  EXPECT_OK(server_socket.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

TEST(SocketCleanup, Datagram) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_posix_socket::DatagramSocket>();
  ASSERT_OK(endpoints.status_value());

  zx::socket client_socket, server_socket;
  ASSERT_OK(zx::socket::create(0, &client_socket, &server_socket));

  fidl::Arena alloc;
  ASSERT_NO_FATAL_FAILURE(ServeAndExerciseFileDescriptionTeardown(
      fuchsia_posix_socket::wire::kDatagramSocketProtocolName,
      fidl::WireResponse<fuchsia_posix_socket::DatagramSocket::Describe>{
          fuchsia_posix_socket::wire::DatagramSocketDescribeResponse::Builder(alloc)
              .socket(std::move(client_socket))
              .tx_meta_buf_size(0)
              .rx_meta_buf_size(0)
              .metadata_encoding_protocol_version(
                  fuchsia_io::wire::UdpMetadataEncodingProtocolVersion::kZero)
              .Build()},
      std::move(endpoints.value())));

  // Client must have disposed of its channel and socket handles on close.
  EXPECT_STATUS(endpoints->client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                     zx::time::infinite_past(), nullptr),
                ZX_ERR_BAD_HANDLE);
  EXPECT_OK(server_socket.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

}  // namespace
