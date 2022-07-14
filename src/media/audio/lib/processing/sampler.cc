// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/sampler.h"

#include <lib/trace/event.h>

#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/point_sampler.h"
#include "src/media/audio/lib/processing/sinc_sampler.h"

namespace media_audio {

std::unique_ptr<Sampler> Sampler::Create(const Format& source_format, const Format& dest_format,
                                         Type type) {
  TRACE_DURATION("audio", "Sampler::Create");

  switch (type) {
    case Type::kDefault:
      if (source_format.frames_per_second() == dest_format.frames_per_second()) {
        return PointSampler::Create(source_format, dest_format);
      }
      return SincSampler::Create(source_format, dest_format);
    case Type::kPointSampler:
      return PointSampler::Create(source_format, dest_format);
    case Type::kSincSampler:
      return SincSampler::Create(source_format, dest_format);
  }
}

}  // namespace media_audio
