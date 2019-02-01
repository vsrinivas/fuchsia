// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_LINK_RING_BUFFER_SOURCE_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_LINK_RING_BUFFER_SOURCE_H_

#include <fbl/ref_ptr.h>

#include "garnet/bin/media/audio_core/audio_link.h"
#include "garnet/bin/media/audio_core/fwd_decls.h"

namespace media {
namespace audio {

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

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_LINK_RING_BUFFER_SOURCE_H_
