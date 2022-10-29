// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_COMMON_TESTING_TEST_SERVER_AND_ASYNC_CLIENT_H_
#define SRC_MEDIA_AUDIO_SERVICES_COMMON_TESTING_TEST_SERVER_AND_ASYNC_CLIENT_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-testing/test_loop.h>
#include <lib/fidl/cpp/client.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/lib/fidl/cpp/include/lib/fidl/cpp/client.h"
#include "src/media/audio/services/common/base_fidl_server.h"
#include "src/media/audio/services/common/fidl_thread.h"

namespace media_audio {

// Creates endpoints for `ProtocolT` or crashes on failure.
template <typename ProtocolT, template <typename T> class ClientT>
std::pair<fidl::ClientEnd<ProtocolT>, fidl::ServerEnd<ProtocolT>> CreateAsyncClientOrDie() {
  auto endpoints = fidl::CreateEndpoints<ProtocolT>();
  if (!endpoints.is_ok()) {
    FX_PLOGS(FATAL, endpoints.status_value()) << "fidl::CreateEndpoints failed";
  }
  return std::make_pair(fidl::ClientEnd(std::move(endpoints->client)),
                        std::move(endpoints->server));
}

template <typename ServerT, template <typename T> class ClientT>
class TestServerAndAsyncClient {
 public:
  using Protocol = typename ServerT::Protocol;

  template <typename... Args>
  explicit TestServerAndAsyncClient(async::TestLoop& test_client_loop,
                                    // args for ServerT::Create
                                    std::shared_ptr<const FidlThread> server_thread, Args... args)
      : loop_(test_client_loop) {
    auto [client, server_end] = CreateAsyncClientOrDie<Protocol, ClientT>();
    server_ = ServerT::Create(std::move(server_thread), std::move(server_end),
                              std::forward<Args>(args)...);
    client_ = ClientT(std::move(client), loop_.dispatcher());

    // We expect the server and async test client to share the same thread and dispatcher.
    EXPECT_TRUE(server_->thread().checker().IsValid());
    EXPECT_EQ(server_->thread().dispatcher(), loop_.dispatcher());
  }

  ~TestServerAndAsyncClient() {
    client_ = ClientT<Protocol>();
    // RunUntilIdle should run all on_unbound callbacks, so the servers should now be shut down.
    loop_.RunUntilIdle();
    EXPECT_TRUE(server_->WaitForShutdown(zx::nsec(0)));
  }

  TestServerAndAsyncClient(const TestServerAndAsyncClient&) = delete;
  TestServerAndAsyncClient& operator=(const TestServerAndAsyncClient&) = delete;

  TestServerAndAsyncClient(TestServerAndAsyncClient&&) noexcept = default;
  TestServerAndAsyncClient& operator=(TestServerAndAsyncClient&&) noexcept = default;

  ServerT& server() { return *server_; }
  std::shared_ptr<ServerT> server_ptr() { return server_; }
  ClientT<Protocol>& client() { return client_; }

 private:
  std::shared_ptr<ServerT> server_;
  ClientT<Protocol> client_;
  async::TestLoop& loop_;
};

// Convenience aliases
// Natural
template <typename ServerT>
using TestServerAndNaturalAsyncClient = TestServerAndAsyncClient<ServerT, fidl::Client>;

template <typename ProtocolT>
std::pair<fidl::ClientEnd<ProtocolT>, fidl::ServerEnd<ProtocolT>> CreateNaturalAsyncClientOrDie() {
  return CreateAsyncClientOrDie<ProtocolT, fidl::Client>();
}

// Wire
template <typename ServerT>
using TestServerAndWireAsyncClient = TestServerAndAsyncClient<ServerT, fidl::WireClient>;

template <typename ProtocolT>
std::pair<fidl::ClientEnd<ProtocolT>, fidl::ServerEnd<ProtocolT>> CreateWireAsyncClientOrDie() {
  return CreateAsyncClientOrDie<ProtocolT, fidl::WireClient>();
}

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_COMMON_TESTING_TEST_SERVER_AND_ASYNC_CLIENT_H_
