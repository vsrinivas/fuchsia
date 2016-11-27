// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>

#include "escher/forward_declarations.h"
#include "escher/renderer/semaphore_wait.h"
#include "escher/vk/vulkan_context.h"
#include "ftl/macros.h"
#include "ftl/memory/ref_counted.h"

namespace escher {

typedef std::function<void()> FrameRetiredCallback;

class Renderer : public ftl::RefCountedThreadSafe<Renderer> {
 public:
  virtual void DrawFrame(Stage& stage,
                         Model& model,
                         const FramebufferPtr& framebuffer,
                         const SemaphorePtr& frame_done,
                         FrameRetiredCallback frame_retired_callback) = 0;

  // Creating a Vulkan Framebuffer requires a render-pass to be specified, which
  // can only be provided by a concrete Renderer subclass.
  virtual FramebufferPtr NewFramebuffer(const ImagePtr& image) = 0;

  const VulkanContext& vulkan_context() { return context_; }

 protected:
  explicit Renderer(impl::EscherImpl* escher);
  virtual ~Renderer();

  // Obtain a CommandBuffer, to record commands for the current frame.
  void BeginFrame(const FramebufferPtr& framebuffer);
  void SubmitPartialFrame();
  void EndFrame(const SemaphorePtr& frame_done,
                FrameRetiredCallback frame_retired_callback);

  impl::CommandBuffer* current_frame() { return current_frame_; }

  impl::EscherImpl* const escher_;
  const VulkanContext context_;

 private:
  impl::CommandBufferPool* pool_;
  impl::CommandBuffer* current_frame_;

  uint64_t frame_number_ = 0;

  FRIEND_REF_COUNTED_THREAD_SAFE(Renderer);
  FTL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

}  // namespace escher
