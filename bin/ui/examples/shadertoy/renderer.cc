// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/renderer.h"

#include "apps/mozart/examples/shadertoy/frame_data.h"
#include "escher/escher.h"
#include "escher/geometry/tessellation.h"
#include "escher/impl/command_buffer_pool.h"
#include "escher/renderer/simple_image_factory.h"
#include "escher/resources/resource_recycler.h"
#include "escher/util/image_utils.h"

Renderer::Renderer(escher::Escher* escher)
    : escher_(escher),
      pool_(escher->command_buffer_pool()),
      rectangle_(escher::NewSimpleRectangleMesh(escher)),
      white_texture_(CreateWhiteTexture()) {}

void Renderer::DrawFrame(const PipelinePtr& pipeline,
                         escher::Framebuffer* framebuffer,
                         escher::Texture* channel0,
                         escher::Texture* channel1,
                         escher::Texture* channel2,
                         escher::Texture* channel3,
                         glm::vec4 i_mouse,
                         float time,
                         escher::SemaphorePtr frame_done) {
  auto command_buffer = pool_->GetCommandBuffer();
  command_buffer->AddSignalSemaphore(std::move(frame_done));

  FTL_CHECK(false);

  // auto data = FrameData(
  // GetChannelTexture(channel0), GetChannelTexture(channel1),
  // GetChannelTexture(channel2), GetChannelTexture(channel3), i_mouse, time);

  // command_buffer_->Submit(context_.queue, )
}

escher::Texture* Renderer::GetChannelTexture(escher::Texture* tex) const {
  return tex ? tex : white_texture_.get();
}

escher::TexturePtr Renderer::CreateWhiteTexture() {
  uint8_t channels[4];
  channels[0] = channels[1] = channels[2] = channels[3] = 255;

  escher::SimpleImageFactory image_factory(escher_->resource_recycler(),
                                           escher_->gpu_allocator());

  auto image = escher::image_utils::NewRgbaImage(
      &image_factory, escher_->gpu_uploader(), 1, 1, channels);
  return ftl::MakeRefCounted<escher::Texture>(
      escher_->resource_recycler(), std::move(image), vk::Filter::eNearest);
}
