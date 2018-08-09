// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/audio_link.h"

#include "garnet/bin/media/audio_core/audio_object.h"

namespace media {
namespace audio {

AudioLink::AudioLink(SourceType source_type, fbl::RefPtr<AudioObject> source,
                     fbl::RefPtr<AudioObject> dest)
    : source_type_(source_type),
      source_(std::move(source)),
      dest_(std::move(dest)),
      valid_(true) {
  // Only outputs and audio ins may be destinations.
  FXL_DCHECK(dest_ != nullptr);
  FXL_DCHECK((dest_->type() == AudioObject::Type::Output) ||
             (dest_->type() == AudioObject::Type::AudioIn));
}

AudioLink::~AudioLink() {}

}  // namespace audio
}  // namespace media
