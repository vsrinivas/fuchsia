// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/ffmpeg/ffmpeg_init.h"

extern "C" {
#include "libavformat/avformat.h"
}

namespace fmlib {

void InitFfmpeg() {
  static bool initialized = []() {
#if LIBAVFORMAT_VERSION_MAJOR == 58
    // TODO(fxb/85336): Get rid of |InitFfmpeg| when we don't have to support V58 anymore.
    av_register_all();
#endif
    return true;
  }();

  (void)initialized;
}

}  // namespace fmlib
