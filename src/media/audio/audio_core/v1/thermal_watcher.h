// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_THERMAL_WATCHER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_THERMAL_WATCHER_H_

#include <fuchsia/thermal/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "src/media/audio/audio_core/v1/thermal_config.h"

namespace media::audio {

class Context;

class ThermalWatcher {
 public:
  static std::unique_ptr<ThermalWatcher> CreateAndWatch(Context& context);

  void SetThermalState(uint64_t state);
  // Assuming thermal state continues to be a singleton for audio (rather than referring to a
  // specific audio device), this should be called whenever new output pipelines are created.
  void SynchronizeThermalState() { SetThermalState(thermal_state_); }

 private:
  ThermalWatcher(fuchsia::thermal::ClientStateWatcherPtr state_watcher, Context& context);
  void WatchThermalState();

  fuchsia::thermal::ClientStateWatcherPtr watcher_;
  async_dispatcher_t* dispatcher_;
  Context& context_;

  uint64_t thermal_state_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_THERMAL_WATCHER_H_
