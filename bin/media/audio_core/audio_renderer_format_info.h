// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_RENDERER_FORMAT_INFO_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_RENDERER_FORMAT_INFO_H_

#include <fbl/ref_counted.h>
#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>

#include "garnet/bin/media/audio_core/fwd_decls.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {
namespace audio {

class AudioRendererFormatInfo
    : public fbl::RefCounted<AudioRendererFormatInfo> {
 public:
  static fbl::RefPtr<AudioRendererFormatInfo> Create(
      fuchsia::media::AudioStreamType format);

  const fuchsia::media::AudioStreamType& format() const { return format_; }
  const TimelineRate& frames_per_ns() const { return frames_per_ns_; }
  const TimelineRate& frame_to_media_ratio() const {
    return frame_to_media_ratio_;
  }
  uint32_t bytes_per_frame() const { return bytes_per_frame_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(AudioRendererFormatInfo);

  AudioRendererFormatInfo(fuchsia::media::AudioStreamType format);

  fuchsia::media::AudioStreamType format_;
  TimelineRate frames_per_ns_;
  TimelineRate frame_to_media_ratio_;
  uint32_t bytes_per_frame_;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_RENDERER_FORMAT_INFO_H_
