// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_DELAY_WATCHER_SERVER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_DELAY_WATCHER_SERVER_H_

#include <fidl/fuchsia.audio/cpp/wire.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <string>

#include "src/media/audio/services/common/base_fidl_server.h"
#include "src/media/audio/services/mixer/common/basic_types.h"

namespace media_audio {

class DelayWatcherServer
    : public BaseFidlServer<DelayWatcherServer, fidl::WireServer, fuchsia_audio::DelayWatcher>,
      public std::enable_shared_from_this<DelayWatcherServer> {
 public:
  struct Args {
    // Name, for debugging only.
    std::string name;

    // The initial delay, if known.
    std::optional<zx::duration> initial_delay;
  };

  // The returned server will live until the `server_end` channel is closed.
  static std::shared_ptr<DelayWatcherServer> Create(
      std::shared_ptr<const FidlThread> fidl_thread,
      fidl::ServerEnd<fuchsia_audio::DelayWatcher> server_end, Args args);

  // Implementation of fidl::WireServer<fuchsia_audio::DelayWatcher>.
  void WatchDelay(WatchDelayRequestView request, WatchDelayCompleter::Sync& completer) final;

  // Updates the current delay.
  void set_delay(zx::duration delay);

 private:
  static inline constexpr std::string_view kClassName = "DelayWatcherServer";
  template <typename ServerT, template <typename T> typename FidlServerT, typename ProtocolT>
  friend class BaseFidlServer;

  explicit DelayWatcherServer(Args args);
  fuchsia_audio::wire::DelayWatcherWatchDelayResponse BuildResponse(fidl::AnyArena& arena);

  const std::string name_;

  std::optional<zx::duration> delay_;
  std::optional<zx::duration> last_sent_delay_;
  std::optional<WatchDelayCompleter::Async> completer_;
  bool first_ = true;
};

// A set of DelayWatcherServers.
class DelayWatcherServerGroup {
 public:
  DelayWatcherServerGroup(std::string_view group_name,
                          std::shared_ptr<const FidlThread> fidl_thread);

  // Adds a new server using the given endpoint.
  void Add(fidl::ServerEnd<fuchsia_audio::DelayWatcher> server_end);

  // Shuts down all servers.
  void Shutdown();

  // Calls `set_delay` on all servers.
  void set_delay(zx::duration delay);

  // Returns the number of live servers.
  int64_t num_live_servers();

 private:
  void GarbageCollect();

  const std::string group_name_;
  const std::shared_ptr<const FidlThread> fidl_thread_;

  std::vector<std::weak_ptr<DelayWatcherServer>> servers_;
  std::optional<zx::duration> delay_;
  int64_t num_created_ = 0;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_DELAY_WATCHER_SERVER_H_
