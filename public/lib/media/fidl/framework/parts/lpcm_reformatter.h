// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_PARTS_LPCM_REFORMATTER_H_
#define SERVICES_MEDIA_FRAMEWORK_PARTS_LPCM_REFORMATTER_H_

#include "services/media/framework/models/transform.h"
#include "services/media/framework/types/audio_stream_type.h"

namespace mojo {
namespace media {

// A transform that reformats samples.
// TODO(dalesat): Some variations on this could be InPlaceTransforms.
class LpcmReformatter : public Transform {
 public:
  static std::shared_ptr<LpcmReformatter> Create(
      const AudioStreamType& in_type,
      const AudioStreamTypeSet& out_type);
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_PARTS_LPCM_REFORMATTER_H_
