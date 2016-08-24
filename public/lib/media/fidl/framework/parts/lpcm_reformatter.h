// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_PARTS_LPCM_REFORMATTER_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_PARTS_LPCM_REFORMATTER_H_

#include "apps/media/services/framework/models/transform.h"
#include "apps/media/services/framework/types/audio_stream_type.h"

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

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_PARTS_LPCM_REFORMATTER_H_
