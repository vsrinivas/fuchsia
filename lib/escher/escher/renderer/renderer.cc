// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/renderer.h"

#include "escher/impl/escher_impl.h"
#include "escher/impl/render_frame.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/framebuffer.h"
#include "escher/renderer/image.h"

namespace escher {

Renderer::Renderer(impl::EscherImpl* escher)
    : escher_(escher), context_(escher_->vulkan_context()) {
  escher_->IncrementRendererCount();
  vk::CommandPoolCreateInfo info;
  info.flags = vk::CommandPoolCreateFlagBits::eTransient |
               vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
  info.queueFamilyIndex = context_.queue_family_index;
  command_pool_ =
      ESCHER_CHECKED_VK_RESULT(context_.device.createCommandPool(info));
}

Renderer::~Renderer() {
  FTL_DCHECK(!current_frame_);

  Cleanup();
  if (!pending_frames_.empty()) {
    context_.device.waitIdle();
    Cleanup();
  }
  FTL_DCHECK(pending_frames_.empty());
  std::vector<vk::CommandBuffer> buffers_to_free;
  buffers_to_free.reserve(free_frames_.size());
  while (!free_frames_.empty()) {
    buffers_to_free.push_back(free_frames_.front()->command_buffer());
    free_frames_.pop();
  }
  context_.device.freeCommandBuffers(
      command_pool_, static_cast<uint32_t>(buffers_to_free.size()),
      buffers_to_free.data());
  context_.device.destroyCommandPool(command_pool_);

  escher_->DecrementRendererCount();
}

impl::RenderFrame* Renderer::BeginFrame(
    const FramebufferPtr& framebuffer,
    const SemaphorePtr& frame_done,
    FrameRetiredCallback frame_retired_callback) {
  FTL_DCHECK(!current_frame_);
  ++frame_number_;
  FTL_LOG(INFO) << "Beginning frame #" << frame_number_;

  // TODO: perhaps do at end of frame?
  Cleanup();

  // Find an existing RenderFrame for reuse, or create a new one.
  if (free_frames_.empty()) {
    vk::CommandBufferAllocateInfo info;
    info.commandPool = command_pool_;
    info.level = vk::CommandBufferLevel::ePrimary;
    info.commandBufferCount = 1;
    auto result =
        ESCHER_CHECKED_VK_RESULT(context_.device.allocateCommandBuffers(info));
    auto render_frame =
        std::make_unique<impl::RenderFrame>(context_, result[0]);
    current_frame_ = render_frame.get();
    pending_frames_.push(std::move(render_frame));
  } else {
    current_frame_ = free_frames_.front().get();
    pending_frames_.push(std::move(free_frames_.front()));
    free_frames_.pop();
  }

  current_frame_->BeginFrame(framebuffer, frame_done,
                             std::move(frame_retired_callback), frame_number_);
  return current_frame_;
}

void Renderer::EndFrame() {
  FTL_DCHECK(current_frame_);
  current_frame_->EndFrameAndSubmit(context_.queue);
  current_frame_ = nullptr;
}

void Renderer::Cleanup() {
  // TODO: add some guard against potential re-entrant calls resulting from
  // invocation of FrameRetiredCallbacks.

  while (!pending_frames_.empty()) {
    auto& frame = pending_frames_.front();
    if (frame->Retire()) {
      free_frames_.push(std::move(pending_frames_.front()));
      pending_frames_.pop();
    } else {
      // The first frame in the queue is not finished, so neither are the rest.
      break;
    }
  }
}

FramebufferPtr Renderer::CreateFramebuffer(
    vk::Framebuffer fb,
    uint32_t width,
    uint32_t height,
    std::vector<ImagePtr> images,
    std::vector<vk::ImageView> image_views) {
  return AdoptRef(new Framebuffer(fb, escher_, this, width, height,
                                  std::move(images), std::move(image_views)));
}

}  // namespace escher
