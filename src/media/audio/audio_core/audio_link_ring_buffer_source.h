// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_RING_BUFFER_SOURCE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_RING_BUFFER_SOURCE_H_

#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/audio_link.h"
#include "src/media/audio/audio_core/fwd_decls.h"

namespace media::audio {

class AudioDevice;

// TODO(johngro): docs
//
class AudioLinkRingBufferSource : public AudioLink {
 public:
  static std::shared_ptr<AudioLinkRingBufferSource> Create(
      fbl::RefPtr<AudioDevice> source, fbl::RefPtr<AudioObject> dest);

 private:
  AudioLinkRingBufferSource(fbl::RefPtr<AudioDevice> source,
                            fbl::RefPtr<AudioObject> dest);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_RING_BUFFER_SOURCE_H_
