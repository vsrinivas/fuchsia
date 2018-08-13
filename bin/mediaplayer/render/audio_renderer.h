// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_RENDER_AUDIO_RENDERER_H_
#define GARNET_BIN_MEDIAPLAYER_RENDER_AUDIO_RENDERER_H_

#include "garnet/bin/mediaplayer/render/renderer.h"

namespace media_player {

// Abstract base class for sinks that render packets.
// TODO(dalesat): Rename this.
class AudioRendererInProc : public Renderer {
 public:
  AudioRendererInProc() {}

  ~AudioRendererInProc() override {}

  // Sets the gain for this renderer.
  virtual void SetGain(float gain) = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_RENDER_AUDIO_RENDERER_H_
