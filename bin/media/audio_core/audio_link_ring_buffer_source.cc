// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/audio_link_ring_buffer_source.h"

#include "garnet/bin/media/audio_core/audio_device.h"

namespace media {
namespace audio {

// static
std::shared_ptr<AudioLinkRingBufferSource> AudioLinkRingBufferSource::Create(
    fbl::RefPtr<AudioDevice> source, fbl::RefPtr<AudioObject> dest) {
  return std::shared_ptr<AudioLinkRingBufferSource>(
      new AudioLinkRingBufferSource(std::move(source), std::move(dest)));
}

AudioLinkRingBufferSource::AudioLinkRingBufferSource(
    fbl::RefPtr<AudioDevice> source, fbl::RefPtr<AudioObject> dest)
    : AudioLink(SourceType::RingBuffer, std::move(source), std::move(dest)) {}

}  // namespace audio
}  // namespace media
