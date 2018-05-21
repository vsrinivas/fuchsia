// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <media/cpp/fidl.h>

#include "garnet/bin/media/media_player/test/fake_audio_renderer.h"
#include "garnet/bin/media/media_player/test/fake_wav_reader.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media_player {
namespace test {

class MediaPlayerTestUnattended {
 public:
  MediaPlayerTestUnattended(std::function<void(int)> quit_callback);

 private:
  std::unique_ptr<component::ApplicationContext> application_context_;
  std::function<void(int)> quit_callback_;
  FakeWavReader fake_reader_;
  FakeAudioRenderer fake_audio_renderer_;
  MediaPlayerPtr media_player_;
};

}  // namespace test
}  // namespace media_player
