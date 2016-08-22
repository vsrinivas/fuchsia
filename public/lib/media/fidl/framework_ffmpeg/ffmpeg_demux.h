// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_FFMPEG_FFMPEG_DEMUX_H_
#define SERVICES_MEDIA_FRAMEWORK_FFMPEG_FFMPEG_DEMUX_H_

#include <memory>

#include "services/media/framework/parts/demux.h"

namespace mojo {
namespace media {

class FfmpegDemux : public Demux {
 public:
  static std::shared_ptr<Demux> Create(std::shared_ptr<Reader> reader);
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_FFMPEG_FFMPEG_DEMUX_H_
