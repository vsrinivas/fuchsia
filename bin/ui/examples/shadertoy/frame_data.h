// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/renderer/texture.h"

// Responsible for the resources required to render a single Shadertoy frame.
// It updates uniform buffers and descriptor sets, and keeps resources alive
// while they are still needed for rendering.
class FrameData {
 public:
  FrameData(escher::Texture* channel0,
            escher::Texture* channel1,
            escher::Texture* channel2,
            escher::Texture* channel3,
            glm::vec4 i_mouse,
            float time);
};
