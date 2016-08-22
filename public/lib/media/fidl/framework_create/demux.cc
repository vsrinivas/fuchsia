// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/media/framework/parts/demux.h"
#include "services/media/framework_ffmpeg/ffmpeg_demux.h"

namespace mojo {
namespace media {

std::shared_ptr<Demux> Demux::Create(std::shared_ptr<Reader> reader) {
  return FfmpegDemux::Create(reader);
}

}  // namespace media
}  // namespace mojo
