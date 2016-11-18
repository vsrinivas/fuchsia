// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/renderer.h"

#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/escher_impl.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/framebuffer.h"
#include "escher/renderer/image.h"

namespace escher {

Renderer::Renderer(impl::EscherImpl* escher)
    : escher_(escher),
      context_(escher_->vulkan_context()),
      pool_(escher->command_buffer_pool()) {
  escher_->IncrementRendererCount();
}

Renderer::~Renderer() {
  FTL_DCHECK(!current_frame_);
  escher_->DecrementRendererCount();
}

void Renderer::BeginFrame(const FramebufferPtr& framebuffer) {
  FTL_DCHECK(!current_frame_);
  ++frame_number_;
  FTL_LOG(INFO) << "Beginning frame #" << frame_number_;
  current_frame_ = pool_->GetCommandBuffer();

  current_frame_->AddWaitSemaphore(
      framebuffer->TakeWaitSemaphore(),
      vk::PipelineStageFlagBits::eColorAttachmentOutput);
}

void Renderer::SubmitPartialFrame() {
  FTL_DCHECK(current_frame_);
  current_frame_->Submit(context_.queue, nullptr);
  current_frame_ = pool_->GetCommandBuffer();
}

void Renderer::EndFrame(const SemaphorePtr& frame_done,
                        FrameRetiredCallback frame_retired_callback) {
  FTL_DCHECK(current_frame_);
  current_frame_->AddSignalSemaphore(frame_done);
  current_frame_->Submit(context_.queue, std::move(frame_retired_callback));
  current_frame_ = nullptr;
}

FramebufferPtr Renderer::CreateFramebuffer(
    vk::Framebuffer fb,
    uint32_t width,
    uint32_t height,
    std::vector<ImagePtr> images,
    std::vector<vk::ImageView> image_views,
    FramebufferPtr extra_framebuffer) {
  return AdoptRef(new Framebuffer(fb, escher_, this, width, height,
                                  std::move(images), std::move(image_views),
                                  std::move(extra_framebuffer)));
}

}  // namespace escher
