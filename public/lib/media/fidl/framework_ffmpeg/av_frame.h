// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_FFMPEG_AV_FRAME_H_
#define SERVICES_MEDIA_FRAMEWORK_FFMPEG_AV_FRAME_H_

extern "C" {
#include "third_party/ffmpeg/libavutil/frame.h"
}

namespace mojo {
namespace media {
namespace ffmpeg {

struct AVFrameDeleter {
  inline void operator()(AVFrame* ptr) const { av_frame_free(&ptr); }
};

using AvFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

struct AvFrame {
  static AvFramePtr Create() { return AvFramePtr(av_frame_alloc()); }
};

}  // namespace ffmpeg
}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_FFMPEG_AV_FRAME_H_
