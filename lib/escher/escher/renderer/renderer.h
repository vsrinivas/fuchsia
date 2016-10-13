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

typedef std::function<void(SemaphorePtr)> FrameRetiredCallback;

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

  // Do periodic housekeeping, such as moving non-longer-pending frames to
  // |free_frames_|.
  void Cleanup();

  const VulkanContext& vulkan_context() { return context_; }

 protected:
  explicit Renderer(impl::EscherImpl* escher);
  virtual ~Renderer();

  // Initializes a RenderFrame, which is automatically placed onto the
  // |pending_frames_| queue.
  impl::RenderFrame* BeginFrame(const FramebufferPtr& framebuffer,
                                const SemaphorePtr& frame_done,
                                FrameRetiredCallback frame_retired_callback);
  void EndFrame();

  FramebufferPtr CreateFramebuffer(vk::Framebuffer,
                                   uint32_t width,
                                   uint32_t height,
                                   std::vector<ImagePtr> images,
                                   std::vector<vk::ImageView> image_views);

  impl::EscherImpl* const escher_;
  const VulkanContext context_;

 private:
  // TODO: access to |command_pool_| needs to be externally synchronized.  This
  // includes implicit uses such as various vkCmd* calls.  The easiest way to
  // guarantee this would be to create all primary/secondary buffers before
  // BeginFrame() is called.  See Vulkan Spec Sec 2.5 under "Implicit Externally
  // Synchronized Parameters".
  vk::CommandPool command_pool_;

  impl::RenderFrame* current_frame_ = nullptr;
  std::queue<std::unique_ptr<impl::RenderFrame>> free_frames_;
  std::queue<std::unique_ptr<impl::RenderFrame>> pending_frames_;
  uint64_t frame_number_ = 0;

  FRIEND_REF_COUNTED_THREAD_SAFE(Renderer);
  FTL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

}  // namespace escher
