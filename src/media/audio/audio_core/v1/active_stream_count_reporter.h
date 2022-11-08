// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ACTIVE_STREAM_COUNT_REPORTER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ACTIVE_STREAM_COUNT_REPORTER_H_

#include "src/media/audio/audio_core/shared/audio_policy.h"
#include "src/media/audio/audio_core/shared/stream_usage.h"

namespace media::audio {

// An interface by which |AudioAdmin| reports changes in active stream counts, by Usage. This
// includes non-FIDL usages such as ULTRASOUND.
class ActiveStreamCountReporter {
 public:
  // To be overridden by child implementations
  virtual void OnActiveRenderCountChanged(RenderUsage usage, uint32_t active_count) {}
  virtual void OnActiveCaptureCountChanged(CaptureUsage usage, uint32_t active_count) {}
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ACTIVE_STREAM_COUNT_REPORTER_H_
