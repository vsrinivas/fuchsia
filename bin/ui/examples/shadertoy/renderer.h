// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/examples/shadertoy/glm_hack.h"
#include "escher/renderer/image.h"
#include "escher/renderer/texture.h"
#include "escher/shape/mesh.h"

class Pipeline;
using PipelinePtr = ftl::RefPtr<Pipeline>;

// This renderer is shared by all ShadertoyState instances, which provide all
// inputs and outputs required to render a frame.
class Renderer {
 public:
  explicit Renderer(escher::Escher* escher);

  void DrawFrame(const PipelinePtr& pipeline,
                 escher::Framebuffer* framebuffer,
                 escher::Texture* channel0,
                 escher::Texture* channel1,
                 escher::Texture* channel2,
                 escher::Texture* channel3,
                 glm::vec4 i_mouse,
                 float time,
                 escher::SemaphorePtr frame_done);

 private:
  // Return a non-null texture, either the one passed in, or a default white
  // texture none was passed in.
  escher::Texture* GetChannelTexture(escher::Texture* texture) const;

  escher::TexturePtr CreateWhiteTexture();

  escher::Escher* escher_;
  escher::impl::CommandBufferPool* pool_;
  escher::MeshPtr rectangle_;
  escher::TexturePtr white_texture_;
};
