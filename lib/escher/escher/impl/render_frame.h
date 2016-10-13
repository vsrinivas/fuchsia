// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <vector>

#include "escher/forward_declarations.h"
#include "escher/renderer/semaphore_wait.h"
#include "escher/vk/vulkan_context.h"

#include "ftl/macros.h"

namespace escher {
typedef std::function<void(SemaphorePtr)> FrameRetiredCallback;

namespace impl {

class GpuAllocator;
class ModelUniformWriter;
class Pipeline;

// RenderFrame is used by a Renderer to draw a single frame.  It retains all
// resources used to render the frame so that they cannot be destroyed while
// the frame's command buffers are pending execution.
//
// TODO: move much of this functionality into a Escher-wide Submission class.
// The reasoning is that:
//   1) Submissions are made to a single queue held by Escher.  If we want to
//      compose Escher renderers, this would make explicit the queue submission
//      order (to really accomplish this, the wait/signal semaphores for the
//      submission would be determined just before submission, e.g. by walking
//      the resources and gathering all their wait semaphores... this supports
//      the scenario where two renderers would use a newly-uploaded texture...
//      only the first renderer to submit needs to wait for it (this statement
//      isn't quite true, but it's premature to delve deeper until we need this
//      functionality)).
class RenderFrame {
 public:
  RenderFrame(const VulkanContext& context, vk::CommandBuffer command_buffer);
  ~RenderFrame();

  void BeginFrame(const FramebufferPtr& framebuffer,
                  const SemaphorePtr& frame_done,
                  FrameRetiredCallback frame_retired_callback,
                  uint64_t frame_number);
  void EndFrameAndSubmit(vk::Queue queue);

  vk::CommandBuffer command_buffer() const { return command_buffer_; }

  // Attempt to reset the frame for reuse.  Return false and do nothing if the
  // frame has not finished rendering (i.e. the submission fence is not ready).
  bool Retire();

  void DrawMesh(const MeshPtr& mesh);

  void AddWaitSemaphore(SemaphorePtr semaphore, vk::PipelineStageFlags stage);

  uint64_t frame_number() const { return frame_number_; }

 private:
  void AddUsedResource(ResourcePtr resource);

  vk::Device device_;
  vk::CommandBuffer command_buffer_;

  std::vector<ResourcePtr> used_resources_;

  std::vector<SemaphorePtr> wait_semaphores_;
  std::vector<vk::PipelineStageFlags> wait_semaphore_stages_;
  std::vector<vk::Semaphore> wait_semaphores_for_submit_;

  uint64_t frame_number_ = 0;
  bool frame_started_ = false;
  bool frame_ended_ = false;

  vk::Fence fence_;
  SemaphorePtr frame_done_semaphore_;
  FrameRetiredCallback frame_retired_callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RenderFrame);
};

}  // namespace impl
}  // namespace escher
