// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/delay_watcher_client.h"

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

// static
std::shared_ptr<DelayWatcherClient> DelayWatcherClient::Create(Args args) {
  struct WithPublicCtor : public DelayWatcherClient {
   public:
    explicit WithPublicCtor(Args args) : DelayWatcherClient(std::move(args)) {}
  };

  if (!args.initial_delay) {
    FX_CHECK(args.client_end);
  }
  if (args.client_end) {
    FX_CHECK(args.thread);
  }

  auto client = std::make_shared<WithPublicCtor>(std::move(args));
  // This cannot happen in the constructor because it requires a shared_ptr, which isn't created
  // until after the constructor returns.
  if (client->client_) {
    client->thread_->PostTask([client]() { client->Loop(); });
  }
  return client;
}

DelayWatcherClient::DelayWatcherClient(Args args)
    : name_(std::move(args.name)),
      thread_(std::move(args.thread)),
      client_(args.client_end ? std::optional(fidl::WireSharedClient(std::move(*args.client_end),
                                                                     thread_->dispatcher()))
                              : std::nullopt),
      delay_(args.initial_delay) {}

void DelayWatcherClient::SetCallback(fit::function<void(std::optional<zx::duration>)> callback) {
  callback_ = std::move(callback);
  if (callback_) {
    callback_(delay_);
  }
}

void DelayWatcherClient::Shutdown() {
  client_ = std::nullopt;
  callback_ = nullptr;
}

void DelayWatcherClient::Loop() {
  fidl::Arena<> arena;
  auto request = fuchsia_audio::wire::DelayWatcherWatchDelayRequest::Builder(arena).Build();

  (*client_)->WatchDelay(request).Then([self = shared_from_this()](auto& result) {
    if (!result.ok()) {
      if (result.is_peer_closed()) {
        FX_LOGS(WARNING) << "DelayWatcherClient '" << self->name_
                         << "' closed unexpectedly: " << result;
      }
      return;
    }

    self->delay_ = result->has_delay() ? std::optional(zx::nsec(result->delay())) : std::nullopt;
    if (self->callback_) {
      self->callback_(self->delay_);
    }
    self->Loop();
  });
}

}  // namespace media_audio
