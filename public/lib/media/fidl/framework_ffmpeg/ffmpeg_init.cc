// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/lock.h"
#include "services/media/framework_ffmpeg/ffmpeg_init.h"

extern "C" {
#include "third_party/ffmpeg/libavformat/avformat.h"
}

namespace mojo {
namespace media {

void InitFfmpeg() {
  static base::Lock lock_;
  static bool initialized_ = false;

  base::AutoLock lock(lock_);
  if (!initialized_) {
    initialized_ = true;
    av_register_all();
  }
}

}  // namespace media
}  // namespace mojo
