// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/ffmpeg/ffmpeg_init.h"

extern "C" {
#include "third_party/ffmpeg/libavformat/avformat.h"
}

namespace mojo {
namespace media {

void InitFfmpeg() {
  static bool initialized = []() {
    av_register_all();
    return true;
  }();

  (void)initialized;
}

}  // namespace media
}  // namespace mojo
