// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_USAGE_GAIN_ADJUSTMENT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_USAGE_GAIN_ADJUSTMENT_H_

#include <fuchsia/media/cpp/fidl.h>

namespace media::audio {

// TODO(36403): Remove duplication of Render/Capturer functions in this interface and AudioAdmin
class UsageGainAdjustment {
 public:
  virtual void SetRenderUsageGainAdjustment(fuchsia::media::AudioRenderUsage usage,
                                            float gain_db) = 0;
  virtual void SetCaptureUsageGainAdjustment(fuchsia::media::AudioCaptureUsage, float gain_db) = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_USAGE_GAIN_ADJUSTMENT_H_
