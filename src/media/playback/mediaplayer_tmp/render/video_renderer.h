// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_RENDER_VIDEO_RENDERER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_RENDER_VIDEO_RENDERER_H_

#include <fuchsia/math/cpp/fidl.h>

#include "src/media/playback/mediaplayer_tmp/render/renderer.h"

namespace media_player {

// Abstract base class for sinks that render packets.
class VideoRenderer : public Renderer {
 public:
  VideoRenderer() {}

  ~VideoRenderer() override {}

  // Returns the current size of the video in pixels.
  virtual fuchsia::math::Size video_size() const = 0;

  // Returns the current pixel aspect ratio of the video.
  virtual fuchsia::math::Size pixel_aspect_ratio() const = 0;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_RENDER_VIDEO_RENDERER_H_
