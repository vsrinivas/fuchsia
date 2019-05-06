// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_link.h"

#include "src/media/audio/audio_core/audio_object.h"

namespace media::audio {

AudioLink::AudioLink(SourceType source_type, fbl::RefPtr<AudioObject> source,
                     fbl::RefPtr<AudioObject> dest)
    : source_type_(source_type),
      source_(std::move(source)),
      dest_(std::move(dest)),
      valid_(true) {
  // Only outputs and AudioCapturers may be destinations.
  FXL_DCHECK(dest_ != nullptr);
  FXL_DCHECK((dest_->type() == AudioObject::Type::Output) ||
             (dest_->type() == AudioObject::Type::AudioCapturer));
}

AudioLink::~AudioLink() = default;

}  // namespace media::audio
