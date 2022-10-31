// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_DELAY_WATCHER_CLIENT_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_DELAY_WATCHER_CLIENT_H_

#include <fidl/fuchsia.audio/cpp/wire.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <string>

#include "src/media/audio/services/common/fidl_thread.h"
#include "src/media/audio/services/mixer/common/basic_types.h"

namespace media_audio {

class DelayWatcherClient : public std::enable_shared_from_this<DelayWatcherClient> {
 public:
  struct Args {
    // Name, for debugging only.
    std::string name;

    // FIDL handle. Required if `initial_delay` not specified. Otherwise optional.
    std::optional<fidl::ClientEnd<fuchsia_audio::DelayWatcher>> client_end;

    // Thread on which this client runs. Required if `client_end` is specified.
    std::shared_ptr<const FidlThread> thread;

    // Required if `client_end` is not specified. Otherwise optional.
    std::optional<zx::duration> initial_delay;
  };

  static std::shared_ptr<DelayWatcherClient> Create(Args args);

  // Returns the current delay, or std::nullopt if the delay is unknown.
  std::optional<zx::duration> delay() const { return delay_; }

  // Sets a callback to invoke each time the delay changes. This is called immediately to report the
  // current delay, then again each time a new delay value is received from the server.
  void SetCallback(fit::function<void(std::optional<zx::duration>)> callback);

  // Shuts down this client. The FIDL connection will be closed.
  void Shutdown();

 private:
  explicit DelayWatcherClient(Args args);
  void Loop();

  const std::string name_;
  const std::shared_ptr<const FidlThread> thread_;
  fit::function<void(std::optional<zx::duration>)> callback_;

  std::optional<fidl::WireSharedClient<fuchsia_audio::DelayWatcher>> client_;
  std::optional<zx::duration> delay_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_DELAY_WATCHER_CLIENT_H_
