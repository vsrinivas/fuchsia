// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_REGISTRY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_REGISTRY_H_

#include <fbl/ref_ptr.h>

namespace media::audio {

class AudioCapturerImpl;
class AudioRendererImpl;

class StreamRegistry {
 public:
  virtual ~StreamRegistry() = default;

  virtual void AddAudioRenderer(fbl::RefPtr<AudioRendererImpl> audio_renderer) = 0;
  virtual void RemoveAudioRenderer(AudioRendererImpl* audio_renderer) = 0;
  virtual void AddAudioCapturer(const fbl::RefPtr<AudioCapturerImpl>& audio_capturer) = 0;
  virtual void RemoveAudioCapturer(AudioCapturerImpl* audio_capturer) = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_REGISTRY_H_
