// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/audio_link_packet_source.h"

#include <trace/event.h>

#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/audio_renderer_impl.h"
#include "src/media/audio/audio_core/format.h"

namespace media::audio {

AudioLinkPacketSource::AudioLinkPacketSource(fbl::RefPtr<AudioObject> source,
                                             fbl::RefPtr<AudioObject> dest,
                                             fbl::RefPtr<Format> format)
    : AudioLink(SourceType::Packet, std::move(source), std::move(dest)), packet_queue_(*format) {}

// static
fbl::RefPtr<AudioLinkPacketSource> AudioLinkPacketSource::Create(fbl::RefPtr<AudioObject> source,
                                                                 fbl::RefPtr<AudioObject> dest,
                                                                 fbl::RefPtr<Format> format) {
  TRACE_DURATION("audio", "AudioLinkPacketSource::Create");
  FX_DCHECK(source);
  FX_DCHECK(dest);

  // TODO(mpuryear): Relax this when other audio objects can be packet sources.
  if (source->type() != AudioObject::Type::AudioRenderer) {
    FX_LOGS(ERROR) << "Cannot create packet source link; packet sources must be AudioRenderers";
    return nullptr;
  }

  return fbl::AdoptRef(
      new AudioLinkPacketSource(std::move(source), std::move(dest), std::move(format)));
}

}  // namespace media::audio
