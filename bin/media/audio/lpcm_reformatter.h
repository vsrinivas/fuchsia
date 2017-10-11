// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/framework/models/transform.h"
#include "garnet/bin/media/framework/types/audio_stream_type.h"

namespace media {

// A transform that reformats samples.
// TODO(dalesat): Some variations on this could be InPlaceTransforms.
class LpcmReformatter : public Transform {
 public:
  static std::shared_ptr<LpcmReformatter> Create(
      const AudioStreamType& in_type,
      AudioStreamType::SampleFormat out_sample_format);

  ~LpcmReformatter() override {}

  // Returns the type of the stream the reformatter will produce.
  virtual std::unique_ptr<StreamType> output_stream_type() = 0;
};

}  // namespace media
