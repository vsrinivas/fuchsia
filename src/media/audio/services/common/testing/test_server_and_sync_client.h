// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_COMMON_TESTING_TEST_SERVER_AND_SYNC_CLIENT_H_
#define SRC_MEDIA_AUDIO_SERVICES_COMMON_TESTING_TEST_SERVER_AND_SYNC_CLIENT_H_

#include <lib/fidl/cpp/channel.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/services/common/base_fidl_server.h"
#include "src/media/audio/services/common/fidl_thread.h"

namespace media_audio {

// Creates endpoints for `ProtocolT` or crashes on failure.
template <typename ProtocolT, template <typename T> class SyncClientT>
std::pair<SyncClientT<ProtocolT>, fidl::ServerEnd<ProtocolT>> CreateSyncClientOrDie() {
  auto endpoints = fidl::CreateEndpoints<ProtocolT>();
  if (!endpoints.is_ok()) {
    FX_PLOGS(FATAL, endpoints.status_value()) << "fidl::CreateEndpoints failed";
  }
  return std::make_pair(SyncClientT<ProtocolT>(std::move(endpoints->client)),
                        std::move(endpoints->server));
}

// Wrapper that includes a test server and a client. The server and client live until this wrapper
// is destroyed. The destructor closes the client side of the connection then blocks until the
// server detects that connection close and shuts itself down.
//
// The type ServerT must be a subclass of BaseFidlServer.
template <typename ServerT, template <typename T> class SyncClientT>
class TestServerAndSyncClient {
 public:
  using Protocol = typename ServerT::Protocol;

  template <typename... Args>
  explicit TestServerAndSyncClient(std::shared_ptr<const FidlThread> thread, Args... args) {
    auto [client, server_end] = CreateSyncClientOrDie<Protocol, SyncClientT>();
    server_ =
        ServerT::Create(std::move(thread), std::move(server_end), std::forward<Args>(args)...);
    client_ = std::move(client);

    // The server should run on a different thread than the synchronous client.
    EXPECT_FALSE(server_->thread().checker().IsValid());
  }

  ~TestServerAndSyncClient() {
    client_ = SyncClientT<Protocol>();
    EXPECT_TRUE(server_->WaitForShutdown(zx::sec(5)));
  }

  TestServerAndSyncClient(const TestServerAndSyncClient&) = delete;
  TestServerAndSyncClient& operator=(const TestServerAndSyncClient&) = delete;

  TestServerAndSyncClient(TestServerAndSyncClient&&) noexcept = default;
  TestServerAndSyncClient& operator=(TestServerAndSyncClient&&) noexcept = default;

  ServerT& server() { return *server_; }
  std::shared_ptr<ServerT> server_ptr() { return server_; }
  SyncClientT<Protocol>& client() { return client_; }

 private:
  std::shared_ptr<ServerT> server_;
  SyncClientT<Protocol> client_;
};

// Convenience aliases
// Natural
template <typename ServerT>
using TestServerAndNaturalSyncClient = TestServerAndSyncClient<ServerT, fidl::SyncClient>;

template <typename ProtocolT>
std::pair<fidl::SyncClient<ProtocolT>, fidl::ServerEnd<ProtocolT>> CreateNaturalSyncClientOrDie() {
  return CreateSyncClientOrDie<ProtocolT, fidl::SyncClient>();
}

// Wire
template <typename ServerT>
using TestServerAndWireSyncClient = TestServerAndSyncClient<ServerT, fidl::WireSyncClient>;

template <typename ProtocolT>
std::pair<fidl::WireSyncClient<ProtocolT>, fidl::ServerEnd<ProtocolT>> CreateWireSyncClientOrDie() {
  return CreateSyncClientOrDie<ProtocolT, fidl::WireSyncClient>();
}

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_COMMON_TESTING_TEST_SERVER_AND_SYNC_CLIENT_H_
