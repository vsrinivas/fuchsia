// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/delay_watcher_client.h"

#include <fidl/fuchsia.audio/cpp/wire.h>
#include <lib/async-testing/test_loop.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {
namespace {

class DelayWatcherServer : public fidl::WireServer<fuchsia_audio::DelayWatcher> {
 public:
  explicit DelayWatcherServer(fidl::ServerEnd<fuchsia_audio::DelayWatcher> server_end,
                              async_dispatcher_t* dispatcher)
      : binding_(fidl::BindServer(dispatcher, std::move(server_end), this,
                                  /*on_unbound=*/nullptr)) {}

  ~DelayWatcherServer() { binding_.Close(ZX_ERR_PEER_CLOSED); }

  // Implementation of fidl::WireServer<fuchsia_audio::DelayWatcher>.
  void WatchDelay(WatchDelayRequestView request, WatchDelayCompleter::Sync& completer) final {
    ASSERT_FALSE(completer_) << "WatchDelay calls cannot be concurrent";
    completer_ = completer.ToAsync();
    return;
  }

  // Releases a pending WatchDelay call, or returns false if there is no pending call.
  bool ReleaseWatchDelay(std::optional<zx::duration> delay) {
    if (!completer_) {
      return false;
    }

    fidl::Arena<> arena;
    auto builder = fuchsia_audio::wire::DelayWatcherWatchDelayResponse::Builder(arena);
    if (delay) {
      builder.delay(delay->to_nsecs());
    }
    completer_->Reply(builder.Build());
    completer_ = std::nullopt;
    return true;
  }

 private:
  fidl::ServerBindingRef<fuchsia_audio::DelayWatcher> binding_;
  std::optional<WatchDelayCompleter::Async> completer_;
};

struct ClientServerWrapper {
  std::shared_ptr<async::TestLoop> loop;
  std::shared_ptr<DelayWatcherClient> client;
  std::shared_ptr<DelayWatcherServer> server;
};

ClientServerWrapper MakeClientServerWrapper(std::optional<zx::duration> initial_delay) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_audio::DelayWatcher>();
  if (!endpoints.is_ok()) {
    FX_PLOGS(FATAL, endpoints.status_value()) << "fidl::CreateEndpoints failed";
  }

  ClientServerWrapper w;
  w.loop = std::make_shared<async::TestLoop>();
  w.client = DelayWatcherClient::Create({
      .client_end = std::move(endpoints->client),
      .thread =
          FidlThread::CreateFromCurrentThread("test_fidl_client_thread", w.loop->dispatcher()),
      .initial_delay = initial_delay,
  });
  w.server =
      std::make_shared<DelayWatcherServer>(std::move(endpoints->server), w.loop->dispatcher());
  return w;
}

struct Callback {
  void Attach(DelayWatcherClient& client) {
    client.SetCallback([this](auto d) {
      value = d;
      have_response = true;
    });
  }
  void Expect(std::optional<zx::duration> expected) {
    EXPECT_TRUE(have_response);
    EXPECT_EQ(value, expected);
    have_response = false;
  }

  std::optional<zx::duration> value;
  bool have_response = false;
};

TEST(DelayWatcherClientTest, FixedDelay) {
  auto client = DelayWatcherClient::Create({.initial_delay = zx::nsec(1000)});
  EXPECT_EQ(client->delay(), zx::nsec(1000));

  Callback callback;
  callback.Attach(*client);
  callback.Expect(zx::nsec(1000));
}

TEST(DelayWatcherClientTest, VariableDelay) {
  auto w = MakeClientServerWrapper(/*initial_delay=*/std::nullopt);
  EXPECT_EQ(w.client->delay(), std::nullopt);

  Callback callback;
  callback.Attach(*w.client);

  // First callback should fire immediately.
  {
    SCOPED_TRACE("first callback");
    callback.Expect(std::nullopt);
  }

  // Send two responses from the server.
  {
    SCOPED_TRACE("first response");
    w.loop->RunUntilIdle();
    ASSERT_TRUE(w.server->ReleaseWatchDelay(zx::nsec(1000)));
    w.loop->RunUntilIdle();
    callback.Expect(zx::nsec(1000));
  }

  {
    SCOPED_TRACE("second response");
    w.loop->RunUntilIdle();
    ASSERT_TRUE(w.server->ReleaseWatchDelay(zx::nsec(2000)));
    w.loop->RunUntilIdle();
    callback.Expect(zx::nsec(2000));
  }
}

TEST(DelayWatcherClientTest, NoCallbackDoesntCrash) {
  auto w = MakeClientServerWrapper(/*initial_delay=*/zx::nsec(99));
  EXPECT_EQ(w.client->delay(), zx::nsec(99));

  // Send two responses from the server.
  {
    SCOPED_TRACE("first response");
    w.loop->RunUntilIdle();
    ASSERT_TRUE(w.server->ReleaseWatchDelay(zx::nsec(1000)));
    w.loop->RunUntilIdle();
  }

  {
    SCOPED_TRACE("second response");
    w.loop->RunUntilIdle();
    ASSERT_TRUE(w.server->ReleaseWatchDelay(zx::nsec(2000)));
    w.loop->RunUntilIdle();
  }
}

}  // namespace
}  // namespace media_audio
